# Đề xuất subnet cho bài Mininet

**Cập nhật:** đã dùng sudo đúng phạm vi project và chạy thành công bài
Mininet h1/h2 trên `10.78.0.0/24`. Không còn vướng mắc quyền root trong lần
kiểm thử này. Đề xuất dưới đây chỉ còn liên quan tới lựa chọn subnet.

## 1. Subnet 10.77.0.0/24 đang được VM sử dụng

Kiểm tra thực tế ngày 09/11/2026 thấy `virbr-qdata` mang IP `10.77.0.1/24`,
có route kết nối `10.77.0.0/24` và interface VM `vnet1` gắn vào bridge đó.
Nếu gán thêm `10.77.0.254/24` vào `mn-gw`, host có hai mạng lớp 2 cùng subnet;
đường trả lời có thể đi sai bridge. Preflight vì vậy từ chối subnet gốc trước
khi tạo topology.

Đề xuất ít tác động nhất: giữ mạng VM, chọn riêng `10.78.0.0/24` cho lab.
Subnet này không xung đột tại thời điểm kiểm tra. Tham số `--subnet` đã hỗ trợ
đồng bộ địa chỉ h1 `.11`, h2 `.12`, gateway `.254` và endpoint của client:

```bash
python3 scripts/preflight.py --subnet 10.78.0.0/24 --json
sudo /usr/bin/python3 mininet/topology.py --subnet 10.78.0.0/24 --test
```

Nếu bắt buộc giữ đúng `10.77.0.0/24`, cần người quản trị quyết định di chuyển
mạng VM hiện có trước. Không tự động xóa hoặc đổi `virbr-qdata`.

## 2. Kết quả sau khi có quyền sudo

Lệnh kiểm thử trên đã PASS: ba lượt ping đều mất gói 0%, client trên h2 nhận
đúng `Temperature = 28.5 C` từ h1 qua EMQX và hai client thoát 0. TLS vẫn xác
thực CA và tên `broker.test` khi kết nối IP `10.78.0.254`.

Đã xác nhận cleanup OVS bridge s1/internal port mn-gw, các veth và route của
lab. `virbr-qdata` vẫn giữ `10.77.0.1/24`; default routes không đổi. Không sửa
firewall hoặc sudoers. Chi tiết trong `docs/KET_QUA_KIEM_THU.md`.
