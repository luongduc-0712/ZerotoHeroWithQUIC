#include "mqtt_codec.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned packets, connacks, publishes; size_t payload; } seen;
static int receive(const mqtt_packet *p, void *arg) {
    seen *s=arg; ++s->packets;
    if(p->type==2) { uint8_t sp,rc; assert(!mqtt_decode_connack(p,&sp,&rc)); assert(!sp && !rc); ++s->connacks; }
    if(p->type==3) {
        mqtt_publish m; assert(!mqtt_decode_publish(p,&m));
        assert(m.topic_len==15 && !memcmp(m.topic,"lab/temperature",15));
        assert(m.payload_len==s->payload);
        for(size_t i=0;i<m.payload_len;++i) assert(m.payload[i]==(uint8_t)i);
        ++s->publishes;
    }
    return 0;
}
static int discard(const mqtt_packet *p, void *arg) { (void)p; (void)arg; return 0; }
static void expect_bad(const uint8_t *data, size_t n) {
    mqtt_parser parser={0}; assert(mqtt_parser_feed(&parser,data,n,discard,NULL)==-1); mqtt_parser_free(&parser);
}
int main(void) {
    mqtt_buffer b={0};
    assert(!mqtt_encode_connect(&b,"abc",20));
    const uint8_t connect[]={0x10,15,0,4,'M','Q','T','T',4,2,0,20,0,3,'a','b','c'};
    assert(b.len==sizeof(connect) && !memcmp(b.data,connect,b.len)); mqtt_buffer_free(&b);
    assert(!mqtt_encode_subscribe(&b,0x1234,"a"));
    const uint8_t sub[]={0x82,6,0x12,0x34,0,1,'a',0};
    assert(b.len==sizeof(sub) && !memcmp(b.data,sub,b.len)); mqtt_buffer_free(&b);
    assert(mqtt_encode_subscribe(&b,0,"a")==-1);
    assert(mqtt_encode_publish(&b,"bad/+",NULL,0)==-1);
    assert(mqtt_encode_publish(&b,"bad/#",NULL,0)==-1);
    assert(mqtt_encode_publish(&b,"\xc0\x80",NULL,0)==-1);
    assert(mqtt_encode_connect(&b,"\xed\xa0\x80",20)==-1);
    const uint8_t connack[]={0x20,2,0,0};
    const size_t lengths[]={0,1,110,111,127,128,16366,16367,16384,65536};
    for(size_t k=0;k<sizeof(lengths)/sizeof(lengths[0]);++k) {
        size_t n=lengths[k]; uint8_t *payload=malloc(n?n:1); assert(payload);
        for(size_t i=0;i<n;++i) payload[i]=(uint8_t)i;
        assert(!mqtt_encode_publish(&b,"lab/temperature",payload,n)); free(payload);
        /* Every boundary for small packets; headers + payload boundaries for large packets. */
        size_t stride=b.len<256 ? 1 : 197;
        for(size_t split=0;split<=b.len;split+=stride) {
            mqtt_parser parser={0}; seen s={.payload=n};
            assert(!mqtt_parser_feed(&parser,b.data,split,receive,&s));
            assert(!mqtt_parser_feed(&parser,b.data+split,b.len-split,receive,&s));
            assert(s.publishes==1 && !parser.len); mqtt_parser_free(&parser);
        }
        mqtt_parser parser={0}; seen s={.payload=n};
        assert(!mqtt_parser_feed(&parser,connack,sizeof(connack),receive,&s));
        for(size_t i=0;i<b.len;++i) assert(!mqtt_parser_feed(&parser,b.data+i,1,receive,&s));
        assert(s.connacks==1 && s.publishes==1); mqtt_parser_free(&parser);
        /* Coalesced packets in one stream read. */
        uint8_t *joined=malloc(b.len*2+sizeof(connack)); assert(joined);
        memcpy(joined,connack,sizeof(connack)); memcpy(joined+sizeof(connack),b.data,b.len);
        memcpy(joined+sizeof(connack)+b.len,b.data,b.len);
        s=(seen){.payload=n};
        assert(!mqtt_parser_feed(&parser,joined,b.len*2+sizeof(connack),receive,&s));
        assert(s.packets==3 && s.publishes==2 && !parser.len);
        mqtt_parser_free(&parser); free(joined); mqtt_buffer_free(&b);
    }
    const uint8_t bad_rl[]={0x30,0x80,0x80,0x80,0x80}; expect_bad(bad_rl,sizeof(bad_rl));
    const uint8_t nonminimal[]={0x30,0x80,0}; expect_bad(nonminimal,sizeof(nonminimal));
    const uint8_t too_big[]={0x30,0xff,0xff,0x7f}; expect_bad(too_big,sizeof(too_big));
    const uint8_t flags[]={0x21,2,0,0}; expect_bad(flags,sizeof(flags));
    const uint8_t qos3[]={0x36,0}; expect_bad(qos3,sizeof(qos3));
    const uint8_t dup_qos0[]={0x38,0}; expect_bad(dup_qos0,sizeof(dup_qos0));
    uint8_t ack[]={0,0}; mqtt_packet p={2,0,ack,2}; uint8_t sp,rc;
    assert(!mqtt_decode_connack(&p,&sp,&rc)); ack[0]=2; assert(mqtt_decode_connack(&p,&sp,&rc)==-1);
    uint8_t suback[]={0,1,0}; p=(mqtt_packet){9,0,suback,3}; uint16_t id; uint8_t qos;
    assert(!mqtt_decode_suback(&p,&id,&qos) && id==1 && qos==0);
    suback[1]=0; assert(mqtt_decode_suback(&p,&id,&qos)==-1);
    uint8_t truncated[]={0,5,'a'}; p=(mqtt_packet){3,0,truncated,3}; mqtt_publish pub;
    assert(mqtt_decode_publish(&p,&pub)==-1);
    puts("MQTT codec tests passed: wire vectors, fragmented/coalesced streams, length boundaries, malformed packets");
    return 0;
}
