#include "mqtt_codec.h"
#include <stdlib.h>
#include <string.h>

static void put16(uint8_t *p, uint16_t n) { p[0]=(uint8_t)(n >> 8); p[1]=(uint8_t)n; }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }

static int valid_utf8(const uint8_t *p, size_t n) {
    for (size_t i=0; i<n;) {
        uint32_t cp=p[i++]; unsigned extra=0; uint32_t min=0;
        if (cp >= 0xc2 && cp <= 0xdf) { cp &= 31; extra=1; min=0x80; }
        else if (cp >= 0xe0 && cp <= 0xef) { cp &= 15; extra=2; min=0x800; }
        else if (cp >= 0xf0 && cp <= 0xf4) { cp &= 7; extra=3; min=0x10000; }
        else if (cp >= 0x80) return 0;
        if (extra > n-i) return 0;
        for (unsigned j=0; j<extra; ++j) {
            if ((p[i] & 0xc0) != 0x80) return 0;
            cp=(cp << 6) | (p[i++] & 63u);
        }
        if (!cp || cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff) ||
            (cp>=0xfdd0 && cp<=0xfdef) || (cp & 0xffff)==0xfffe || (cp & 0xffff)==0xffff) return 0;
    }
    return 1;
}
static int valid_topic(const uint8_t *p, size_t n) {
    return n && n<=UINT16_MAX && valid_utf8(p,n) && !memchr(p,'+',n) && !memchr(p,'#',n);
}
void mqtt_buffer_free(mqtt_buffer *b) { free(b->data); b->data=NULL; b->len=0; }
void mqtt_parser_free(mqtt_parser *p) { free(p->data); memset(p,0,sizeof(*p)); }
static int allocate(mqtt_buffer *b, uint8_t header, size_t remaining, uint8_t **body) {
    uint8_t encoded[4]; size_t nh=0, n=remaining;
    if (remaining>268435455u || remaining>MQTT_MAX_PACKET_SIZE-5u) return -1;
    do { encoded[nh]=(uint8_t)(n%128); n/=128; if (n) encoded[nh]|=128; ++nh; } while(n);
    b->data=malloc(1+nh+remaining); if (!b->data) return -1;
    b->len=1+nh+remaining; b->data[0]=header; memcpy(b->data+1,encoded,nh);
    *body=b->data+1+nh; return 0;
}
int mqtt_encode_connect(mqtt_buffer *b, const char *id, uint16_t keepalive) {
    size_t n=strlen(id); uint8_t *p;
    if (!n || n>UINT16_MAX || !valid_utf8((const uint8_t *)id,n) || allocate(b,0x10,12+n,&p)) return -1;
    const uint8_t prefix[]={0,4,'M','Q','T','T',4,2};
    memcpy(p,prefix,sizeof(prefix)); put16(p+8,keepalive); put16(p+10,(uint16_t)n); memcpy(p+12,id,n); return 0;
}
int mqtt_encode_subscribe(mqtt_buffer *b, uint16_t id, const char *topic) {
    size_t n=strlen(topic); uint8_t *p;
    if (!id || !valid_topic((const uint8_t *)topic,n) || allocate(b,0x82,5+n,&p)) return -1;
    put16(p,id); put16(p+2,(uint16_t)n); memcpy(p+4,topic,n); p[4+n]=0; return 0;
}
int mqtt_encode_publish(mqtt_buffer *b, const char *topic, const uint8_t *payload, size_t n) {
    size_t nt=strlen(topic); uint8_t *p;
    if (!valid_topic((const uint8_t *)topic,nt) || n>MQTT_MAX_PACKET_SIZE || (n && !payload) ||
        allocate(b,0x30,2+nt+n,&p)) return -1;
    put16(p,(uint16_t)nt); memcpy(p+2,topic,nt); if(n) memcpy(p+2+nt,payload,n); return 0;
}
int mqtt_encode_pingreq(mqtt_buffer *b) { uint8_t *p; return allocate(b,0xc0,0,&p); }
int mqtt_encode_disconnect(mqtt_buffer *b) { uint8_t *p; return allocate(b,0xe0,0,&p); }

/* Return 0 for incomplete input, 1 for a complete header, -1 for invalid. */
static int header(const uint8_t *p, size_t n, size_t *nh, size_t *body_len) {
    if (!n) return 0;
    unsigned type=p[0] >> 4, flags=p[0] & 15;
    if (!type || type==15) return -1;
    unsigned required=(type==6 || type==8 || type==10) ? 2u : 0u;
    if ((type!=3 && flags!=required) || (type==3 && (((flags>>1)&3)==3 || (!(flags&6) && (flags&8))))) return -1;
    size_t value=0, mult=1;
    for (size_t i=1; i<=4; ++i) {
        if (i>=n) return 0;
        value+=(size_t)(p[i]&127)*mult;
        if (!(p[i]&128)) {
            if (i>1 && !(p[i]&127)) return -1;
            *nh=i+1; *body_len=value;
            return value>MQTT_MAX_PACKET_SIZE-*nh ? -1 : 1;
        }
        mult*=128;
    }
    return -1;
}
int mqtt_parser_feed(mqtt_parser *p, const uint8_t *data, size_t n, mqtt_packet_cb cb, void *arg) {
    if ((!data && n) || !cb) return -1;
    while(n || p->len) {
        size_t nh=0, nb=0; int hr=header(p->data,p->len,&nh,&nb);
        if (hr<0) return -1;
        if (hr==1 && p->len>=nh+nb) {
            mqtt_packet packet={p->data[0] >> 4,p->data[0]&15,p->data+nh,nb};
            if (cb(&packet,arg)) return -1;
            p->len-=nh+nb; memmove(p->data,p->data+nh+nb,p->len); continue;
        }
        if (!n) break;
        /* Read the header one byte at a time; never reserve an unchecked length. */
        size_t need=hr ? nh+nb : p->len+1;
        if (need>p->cap) {
            uint8_t *q=realloc(p->data,need); if(!q) return -1; p->data=q; p->cap=need;
        }
        size_t take=need-p->len; if(take>n) take=n;
        memcpy(p->data+p->len,data,take); p->len+=take; data+=take; n-=take;
    }
    return 0;
}
int mqtt_decode_connack(const mqtt_packet *p, uint8_t *sp, uint8_t *rc) {
    if(p->type!=2 || p->flags || p->len!=2 || p->body[0]>1 || p->body[1]>5 || (p->body[1] && p->body[0])) return -1;
    *sp=p->body[0]; *rc=p->body[1]; return 0;
}
int mqtt_decode_suback(const mqtt_packet *p, uint16_t *id, uint8_t *qos) {
    if(p->type!=9 || p->flags || p->len!=3 || !get16(p->body) || (p->body[2]>2 && p->body[2]!=0x80)) return -1;
    *id=get16(p->body); *qos=p->body[2]; return 0;
}
int mqtt_decode_publish(const mqtt_packet *p, mqtt_publish *out) {
    if(p->type!=3 || (p->flags&14) || p->len<2) return -1;
    size_t nt=get16(p->body);
    if(nt>p->len-2 || !valid_topic(p->body+2,nt)) return -1;
    out->topic=p->body+2; out->topic_len=nt; out->payload=p->body+2+nt; out->payload_len=p->len-2-nt;
    out->retained=p->flags&1; return 0;
}
