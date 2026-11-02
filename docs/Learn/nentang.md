1. Process
    - Giả sử có client.c => compile: gcc client.c -o client => client chỉ là file executable trên ổ.
    - khi ./ client => tạo một process chạy client.c (phần này chạy trong user). Phần này không được trực tiếp điều khiển mạng, tức là các tầng Application của các end-point không tự gửi payload, giữa chúng có kernel.

2. User space và Kernel space
        ┌──────────────────────────────────────────────┐
        │                  USER SPACE                  │
        │                                              │
        │        Process client       Process server   │
        │                                              │
        ├──────────────────────────────────────────────┤
        │                  KERNEL SPACE                │
        │                                              │
        │        Socket                                │
        │          │                                   │
        │        TCP / UDP                             │
        │          │                                   │
        │         IP                                   │
        │          │                                   │
        │     Network Driver                           │
        │          │                                   │
        └──────────┼───────────────────────────────────┘
                   │
                   ▼
              Network Card
                   │
                   ▼
                Network
            
    Khi ta viết send(); nằm ở user (yêu cầu từ Application). Còn TCP/IP ở Kernel space

    ==> Như vậy cần một cách để application layer yêu cầu kernel rằng "muốn sử dụng network" để nhận/gửi dữ liệu
    ==> Socket API xuất hiện                                                                                                                            