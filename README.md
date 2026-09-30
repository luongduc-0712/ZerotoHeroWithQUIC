# ZerotoHeroWithQUIC

Research, socket programming implementations, and performance benchmarks for the QUIC protocol.
Đặt vấn đề
Giả sử chúng ta có máy tính A và B kết nối mạng LAN, và chúng ta muốn gửi dưx liệu từ máy A sang máy B (với điều kiện chúng ta đã biết địa chỉ IP của cả 2 máy và 2 máy đã kết nối thành công)
Bây giờ từ máy A, ta muốn chào hỏi máy B với nội dung “Hello”, vậy làm cách nào để máy B nhận được dữ liệu?
Bơi vì, dữ liệu nằm ở tầng ứng dụng (user space), chúng ta cần gửi vào kenel space, sau đó kernel từ máy A-> kernel B. Nhưng làm sao để truyền tài được dữ liệu từ user space xuống kernel space và ngược lại? Trong kernel space dữ liệu sẽ được xử lý như thế nào? 
Đó là 2 vấn đề cần giải đáp
Các khái niệm cơ bản
Các khái niệm và cách các giao thức hoạt động cơ bản: IP, Port, Protocol, Server - Client
IP - địa chỉ
IP như địa chỉ nhà, khi bên A biết đia chỉ của B thì có thể gửi dữ liệu từ A đến.
Giả sử máy B có nhiều chương trình đang chạy, vậy làm thế nào có thể gửi dữ liệu đến đúng chương trình cần dữ liệu đó => Port
Port
Port là một số được sử dụng ở tầng transport, giúp phân biệt các điểm giao tiếp
Để kết nối từ máy A đến máy B, chúng ta cần biết được địa chỉ IP của máy B và Port mà máy B sử dụng
TCP
Thiết lập kết nối: cần làm rõ
Cung cấp byte stream: cần làm rõ
Đảm bảo dữ liệu giao cho ứng dụng một cách tuần tự, đúng dữ liệu: cần làm rõ
Cơ chế truyền lại khi cần: cần làm rõ
UDP
Không cần thiết lập kết nối: cần làm rõ
Truyền theo datagram: cần làm rõ
Không đảm bảo dữ liệu đến đúng nơi hay đúng thứ tự: cần làm rõ
Không cung cấp cơ chế truyền lại đáng tin cậy: cần làm rõ
Client và Server
Server là chương trình cung cấp dịch vụ và chờ Client kết nối
Client là chương trình chủ động kết nối với Server
Cần làm rõ hơn nữa

⇒ Tóm lại dữ liệu sẽ đi như sau: Chương trình trên Client -> Socket API -> Giao thức truyền tải -> Kết nối giữa 2 máy -> kernel trên máy tính -> Socket API -> Chương trình trên Server
 








Vấn đề 1: Dữ liệu được đưa xuống kernel space như thế nào?
Cách xử lý: tạo một giao diện để gửi dữ liệu, đó là socket API
Khái niệm: socket là một điểm cuối giao tiếp, giống như shipper (giao hàng tại 2 điểm đầu, cuối). 
Cần hiểu rõ: 
socket: là điểm giao tiếp nằm ở giữa user và kernel
socket API: là giao diện, chương trình chứa các hàm để xử lý dữ liệu
programming socket: là công việc lập trình các API
làm rõ: socket có chứa socket API không?
Chúng ta sẽ sử dụng các hàm như sau:
socket(): tạo socket
bind(): gắn socket với địa chỉ cục bộ (làm rõ: địa chỉ cục bộ)
listen(): chuẩn bị tiếp nhận kết nối
accept(): lấy một kết nối đã thiết lập
connect(): chủ động kết nối
send(): gửi dữ liệu
recv(): nhận dữ liệu
close(): đóng file descriptor

	=> Tóm lại chương trình giao tiếp với hệ điều hành qua socket API

Thư viện cơ bản
<sys/socket.h>: socket(), bind(), listen(), accept(), struct sockaddr và socklen_t
<netinet/in.h>: sockaddr_in, AF_INET, INADDR_ANY, htons() và ntohs()
<arpa/inet.h>: inet_ntop() để chuyển địa chỉ IP dạng nhị phân thành chuỗi dễ đọc
<unistd.h>: close()
<stdio.h>: printf(), perror() và getchar()
Hàm socket(): tạo socket và file descriptor 
FIle desciptor: là một số nguyên không âm mà tiến trình sử dụng để tham chiếu đến một tài nguyên I/O.
Chẳng hạn: một chương trình sử dụng 2 tài nguyên I/O khác nhau, ví dụ như mở file và tạo socket, như vậy ta sẽ có 1 tham số fd để chương trình có thể tham chiếu vào tài nguyên cần thiết. Ví dụ: read(fd, buf, 100); tức là chương trình yêu cầu đọc tối đa 100 byte từ tài nguyên được tham chiếu bởi fd, ghi vào bộ nhớ đệm buf
Khi chạy program, kernel tạo process để thực thi chương trình, mỗi process có một fd riêng

Hàm socket(): được khai báo trong thư viện <sys/socket.h>
Câu lệnh: int socket(int domain, int type, int protocol)
Tham số domain: 
AF_INET xác định địa chỉ mà mà IPv4 sử dụng
AF_INET xác định địa chỉ mà IPv6 sử dụng
AF_UNIX giao tiếp nội bộ trên cùng 1 máy
Tham số type: 
		SOCK_STREAM: luồng byte, thường sử dụng TCP
		SOCK_DIAGRAM: datagra, thường sử dụng UDP
Tham số Protocol: xác định giao thức cần được sử dụng, khi đặt bằng 0, kernel tự chọn giao thức phù hợp với domain và type. Hoặc có thể ghi rằng: IPPROTOCOL_TCP/UDP.
	⇒ Các tham số trên chỉ mô tả socket chúng ta muốn tạo
Trình bày về kernel xác nhận các số tham chiếu
Với mỗi process sẽ có bảng tham chiếu riêng, vì vậy, nếu trong cùng 1 tiến trình sẽ không cùng giá trị File Descriptors. Trong trường hợp nhận 2 process, các giá trị File Descriptors có thể có giá trị khác nhau, nhưng khác bảng tham chiếu.
Cách Kernel xác định được bảng tham chiếu ứng với process thông qua Process ID (PID).
Có thể kiểm tra PID của mỗi process: 
pgrep  -a “tên chương trình”: tìm PID
ls -l /proc/”PID”/fd: Xem bảng mà kernel cung cấp qua hệ thống
Hàm bind(): Giả sử máy A có IP1, Port1, tất nhiên nó sẽ có nhiều Port nữa, chúng ta muốn dùng Port1 để kết nối với máy khác. Vậy chúng ta gắn địa chỉ IP và Port của máy A cho socket đang chạy ở máy A.
Cấu trúc hàm bind():
      int bind(
	int sockfd,
	const struct sockaddr *addr,
	socklen_t addrlen
      );
Như vậy với struct chúng ta sẽ mang các trường dữ liệu bao gồm: họ địa chỉ IPv4, Port cục bộ và địa chỉ IP cục bộ. Chúng ta khai báo như sau:
       struct sockaddr_in sockaddr(
	sin_family: họ địa chỉ IPv4
	sin_port: Port cục bộ
	sin_addr: IP cục bộ
       );
⇒ Lưu ý: với địa chỉ Port, trong C chúng ta đang có là một số nguyên 16 bit, có 2 cách sắp xếp lưu giá trị trong bộ nhớ là big-endian và little-endian. Đó là trên máy tính, còn với network, quy ước sử dụng big-endian. Để phù hợp, chúng ta luôn cần htons() hoặc htonl() với 32 bits như IP.
Hàm listen(): trong như viện #include <sys/socket.h>
Câu lệnh: int listen(int socketfd, int backlog);
Khi server gọi hàm listen(),  nó yêu cầu kernel chuyển socket sang trạng thái lắng nghe các kết nối và thiết lập giới hạn hàng đơi kết nối, khi client gọi hàm connection(), kernel bên client gửi gói SYN đến server, sau đó kernel của server gửi trả lại gói SYN-ACK, client gửi lại ACK sau khi nhận được SYN-ACK của server.
Tuy nhiên client có thể gửi dữ liệu cho kernel bên server, dù socket bên server có thể chưa nhận nếu chưa gọi hàm accept().
backlog: xác định giới hạn hàng đợi các kết nối TCP đã thiết lập thành công nhưng chưa được chương trình tiếp nhận bằng accept()
Hàm accept(): khai báo trong thư viện #include <sys/socket.h>
Nguyên mẫu: 
                 int accept(
		int sockfd,
		struct sockaddr *addr,
		socklen_t *addr_len
     );
Khi sử dụng hàm accept(), socket yêu cầu kernel lấy một kết nối đã được thiết lập trên listening socket và trả về FD của connected socket. Trước đó kernel đã có thông tin của IP và Port của client, accept() sao chép thông tin này và ghi vào vùng nhớ server.
Tham số addr sử dụng con trỏ để ghi thông tin địa chỉ Client vào vùng nhớ mà chương trình đã chuẩn bị. Nếu không dùng con trỏ thì không thể ghi được IP và Port vào vùng nhớ.
Tham số addrlen dùng con trỏ vì kernel cần đọc kích thước vùng nhớ ban đầu và cập nhật biến này bằng kích thước địa chỉ thực tế.
