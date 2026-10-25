<div align="center">
  <img src="resources/calendar-deck.png" width="120" alt="Biểu tượng lịch xanh Calendar Deck với số 36">
  <h1>Calendar Deck</h1>
  <p><strong>Cả tuần của bạn, ngay trên desktop.</strong></p>
  <p>Ứng dụng lịch Windows gọn nhẹ, hỗ trợ Google iCal và file ICS offline.</p>
  <p><a href="README.md">English</a> · <strong>Tiếng Việt</strong></p>
  <p>
    <img alt="Windows 10 và 11" src="https://img.shields.io/badge/Windows-10%20%2F%2011-0078D4">
    <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C">
    <img alt="Phiên bản 0.2.4" src="https://img.shields.io/badge/version-0.2.4-2563EB">
  </p>
</div>

---

## Xem trọn tuần, nắm rõ lịch của mình

Calendar Deck hiển thị lịch **Thứ Hai–Chủ Nhật**, đầy đủ khung giờ **00:00–24:00** trên Windows. Chọn **Desktop mode** để gắn lịch vào lớp nền phía sau các cửa sổ, hoặc **Floating mode** để dùng như một cửa sổ bình thường.

Ứng dụng viết bằng **C++20, Win32, Direct2D và DirectWrite**. Bản đóng gói không yêu cầu Electron, trình duyệt nhúng, Python hay Node.js.

| Tính năng | Bạn có thể làm gì? |
| --- | --- |
| 3 chế độ nhìn | Chuyển đổi linh hoạt giữa Tuần (Week), Tháng (Month) và Năm (Year) |
| Lịch tuần | Xem đủ 7 ngày, đánh dấu hôm nay và vạch giờ hiện tại với thiết kế bo cong mềm mại |
| Lịch tháng | Toàn cảnh các tuần trong tháng, hiển thị sự kiện theo ngày, nhấp để xem tuần chi tiết |
| Lịch năm | Toàn cảnh 12 tháng với số lượng sự kiện và chấm đánh dấu ngày có lịch |
| Nhiều lịch | Đặt tên riêng, chọn một trong 5 màu, bật/tắt hiển thị |
| Nguồn online | Đọc Google iCal, kiểm tra tự động, giữ cache khi mất mạng |
| File offline | Nhập ICS và thay file mới khi thời khóa biểu thay đổi |
| Bố trí sự kiện | Xem lịch lặp được hỗ trợ, cả ngày, qua đêm và trùng giờ |
| Điều khiển cửa sổ | Thu nhỏ, phóng to/khôi phục, menu khay và tùy chọn khi đóng |
| Widget desktop | Di chuyển, đổi kích thước khi mở khóa; khóa vị trí từ menu |
| Lưu trữ cục bộ | Bảo vệ URL, nội dung lịch và cấu hình bằng Windows DPAPI |

## Bắt đầu nhanh

### 1. Mở ứng dụng

Với bản đóng gói, giải nén **CalendarDeck-v0.2.4-windows-x64.zip** vào thư mục bạn muốn giữ lâu dài, rồi mở **CalendarDeck.exe**. Không cần trình cài đặt hay quyền quản trị viên.

Gói gồm EXE, hướng dẫn Anh/Việt và script tạo shortcut. Nếu dùng mã nguồn, xem phần [build](#build-từ-mã-nguồn).

**Yêu cầu:** Windows 10 phiên bản 1903 trở lên hoặc Windows 11, bản x64. App dùng SQLite và ICU có sẵn trong Windows. Nguồn URL cần mạng để lấy dữ liệu mới; lịch nhập từ file hoạt động offline.

### 2. Chọn cách thêm lịch

Mở **Settings → Calendars**.

| Add URL Calendar — Thêm bằng link | Import .ics — Nhập file |
| --- | --- |
| Dùng nguồn Google iCal hoặc link chia sẻ được hỗ trợ | Chọn file iCalendar trên máy |
| Tự kiểm tra cập nhật | Giữ một bản lịch offline |
| Cần quyền truy cập nguồn khi tải dữ liệu mới | Không cần đăng nhập Google hay kết nối mạng |
| Vẫn xem cache khi mất mạng | Bấm Replace .ics khi lịch thay đổi |

### 3. Tạo biểu tượng trên Desktop

Trong thư mục đã giải nén, chạy:

```powershell
.\create-desktop-shortcut.ps1
```

Nếu đang ở thư mục mã nguồn, sau khi đóng gói chạy:

```powershell
.\scripts\create-desktop-shortcut.ps1
```

Bạn cũng có thể dùng menu chuột phải của Windows trên CalendarDeck.exe để tạo shortcut. Icon lịch xanh số **36** đã nhúng vào EXE; app không cần file icon ngoài để chạy. Nếu chuyển EXE sang nơi khác, hãy tạo lại shortcut.

## Thêm Google Calendar bằng URL

1. Mở Google Calendar trên máy tính.
2. Vào **Cài đặt → chọn lịch → Tích hợp lịch**.
3. Sao chép **Địa chỉ bí mật ở định dạng iCal**, hoặc **Địa chỉ công khai ở định dạng iCal** nếu nguồn lịch cho phép truy cập công khai.
4. Trong Calendar Deck, chọn **Add URL Calendar**.
5. Đặt tên, dán URL, chọn màu và trạng thái hiển thị, rồi bấm **Save calendar**.

App nhận link HTTPS iCal kết thúc bằng `.ics`. Link chia sẻ/xem lịch Google có một mã lịch trong `src=` hoặc `cid=` base64 sẽ được chuyển ngay trên máy sang iCal công khai trước khi lưu. Link trang chủ Calendar, link một sự kiện, mã HTML iframe hoặc link chứa nhiều lịch không dùng được như một nguồn duy nhất.

- **Sync now**: kiểm tra lịch URL đang chọn.
- **Sync** trên cửa sổ chính hoặc **Sync all calendars** ở khay: kiểm tra tất cả nguồn URL.
- Mặc định kiểm tra mỗi **15 phút**; đổi trong **General**, từ **5 đến 1440 phút**.
- Nếu tải thất bại, bản cache hợp lệ trước đó vẫn được giữ.
- Google có thể chậm cập nhật nguồn iCal; đây là đồng bộ đọc một chiều, không phải cập nhật tức thì hai chiều.

**URL bí mật:** giữ kín như mật khẩu. Không đưa vào issue, ảnh công khai hoặc repository.

**URL công khai:** chỉ hoạt động khi chủ lịch và tổ chức cho phép nguồn công khai. Mở được link bằng trình duyệt đang đăng nhập không có nghĩa app được truy cập. Nếu nguồn chỉ cung cấp rảnh/bận, nó không chứa tên và mô tả để app hiển thị. Calendar Deck không thay đổi quyền chia sẻ Google của bạn.

## Nhập file ICS để xem offline

1. Chuẩn bị file `.ics` chứa lịch của bạn.
2. Vào **Settings → Calendars → Import .ics...**.
3. Chọn file. App nhập và lưu ngay; lịch có nhãn **(file)** trong danh sách.
4. Nếu muốn, sửa tên, màu hoặc bật/tắt hiển thị rồi bấm **Save calendar**.
5. Nhấp sự kiện trên lịch tuần để đọc tên đầy đủ, lịch nguồn, thời gian, địa điểm và mô tả.

App chép bản lịch vào bộ nhớ lưu trữ cục bộ có mã hóa. Sau khi nhập, di chuyển hoặc xóa file gốc không làm mất lịch trong app. Lịch file hiển thị thời điểm nhập gần nhất và không được tải qua mạng.

### Khi thời khóa biểu thay đổi

Chọn đúng lịch đã nhập, bấm **Replace .ics...**, rồi chọn file mới. Các sự kiện được thay bằng bản mới; **tên lịch, màu và trạng thái hiển thị vẫn giữ nguyên**. Nếu file lỗi hoặc không hợp lệ, bản cũ được giữ lại.

Nên dùng Replace .ics thay vì nhập mỗi bản cập nhật thành một lịch mới, để tránh hiển thị trùng.

### Lấy file từ Google Calendar

Nếu tài khoản cho phép, trên máy tính vào **Cài đặt → Nhập và xuất → Xuất**. Giải nén ZIP tải về rồi chọn file `.ics` của lịch cần dùng. Tài khoản trường/cơ quan có thể bị quản trị viên hạn chế xuất dữ liệu. Bạn cũng có thể dùng file ICS hợp lệ từ nguồn thời khóa biểu của mình.

App nhận ICS mã hóa UTF-8, gồm UTF-8 BOM, tối đa **10 MiB mỗi file**. Cần giải nén ZIP trước. Nếu file đã bị lược bỏ thông tin chi tiết và chỉ còn Busy/rảnh bận, nhập lại không thể khôi phục các thông tin không có trong file.

## Sử dụng hằng ngày

| Thao tác | Cách thực hiện |
| --- | --- |
| Chuyển Tuần / Tháng / Năm | Nút chuyển dạng viên thuốc (pill) trên toolbar, menu chuột phải hoặc Ctrl+1 / Ctrl+2 / Ctrl+3 |
| Tuần / Tháng / Năm trước & sau | Nút mũi tên ‹ / › hoặc phím mũi tên Trái / Phải |
| Thời gian hiện tại | **Hôm nay** hoặc phím Home |
| Xem chi tiết sự kiện | Nhấp sự kiện; rê chuột để xem tooltip |
| Xem sự kiện cả ngày | Nhấp dải cả ngày của ngày đó |
| Đọc lỗi/cảnh báo nguồn lịch | Nhấp thanh trạng thái phía dưới |
| Đổi tên/màu lịch | Settings → Calendars → chọn lịch → Save calendar |
| Ẩn lịch nhưng vẫn giữ nguồn | Bỏ chọn **Show events on calendar**, rồi lưu |
| Xóa lịch khỏi app | **Remove calendar** → xác nhận; sự kiện Google không bị xóa |
| Mở Settings | Nút trên toolbar hoặc Ctrl+S |
| Kiểm tra nguồn URL | Sync hoặc F5 |
| Ẩn widget | Esc hoặc menu ở khay |

**Nhớ lưu trước khi chuyển:** Settings sẽ cảnh báo trước khi bỏ các chỉnh sửa chưa lưu khi đổi trang hoặc đổi lịch.

### Floating và Desktop

**Floating** là cửa sổ bình thường, có nút ở taskbar, thu nhỏ, phóng to/khôi phục và đóng. Kéo vùng tiêu đề để di chuyển, kéo cạnh để đổi kích thước.

**Desktop** gắn widget vào Explorer, phía sau các cửa sổ ứng dụng. Khi mở khóa, kéo header hoặc cạnh để di chuyển/đổi kích thước. Chế độ này không cho phóng to. Chuột phải trên lịch hoặc icon ở khay để chuyển chế độ và bật/tắt **Lock widget position**. Phần mềm wallpaper và cách hoạt động của Explorer có thể ảnh hưởng chế độ Desktop.

### Đóng, ẩn và thoát hẳn

Trong **Settings → General**, chọn nút Close sẽ ẩn xuống khay hay thoát. Nhấp đúp icon khay để hiện tuần hiện tại. Chọn **Exit** ở menu khay để dừng app hoàn toàn. Nếu đang tải mạng, app có thể cần chờ tác vụ hiện tại kết thúc.

## Cập nhật và dữ liệu trên máy

| Chế độ | Vị trí |
| --- | --- |
| Bình thường | `%LOCALAPPDATA%\CalendarDeck\calendar-deck.db` |
| Portable (`--portable`) | `data\calendar-deck.db` cạnh EXE |

Cấu hình, nguồn lịch, file nhập và lỗi đồng bộ được mã hóa bằng Windows DPAPI trước khi lưu SQLite. Dữ liệu portable vẫn gắn với tài khoản Windows hiện tại, không phải định dạng sao lưu để mang sang tài khoản khác. Hãy giữ file ICS gốc hoặc URL nguồn nếu cần chuyển máy/tài khoản.

Khi nâng cấp: chọn **Exit ở khay**, thay file ứng dụng rồi mở lại. Dữ liệu chế độ bình thường vẫn nằm trong AppData. Nếu dùng portable, giữ nguyên thư mục `data`. App tự đọc được cấu hình một lịch của bản cũ.

Để thử bằng sự kiện mẫu, không kết nối lịch:

```powershell
.\CalendarDeck.exe --portable --demo
```

Nên dùng một thư mục giải nén riêng cho demo. Mỗi thư mục dữ liệu chỉ chạy một instance; thoát instance đó trước khi đổi giữa demo và lịch thật.

## Xử lý sự cố

| Hiện tượng | Cách kiểm tra |
| --- | --- |
| Lịch hiện Busy hoặc thiếu tên môn | Kiểm tra quyền nguồn và nội dung ICS; nguồn rảnh/bận không có dữ liệu chi tiết bị thiếu |
| Public feed không truy cập được | Kiểm tra quyền xem không cần đăng nhập và chính sách xuất iCal của tổ chức |
| Không có Secret address | Có thể do tài khoản trường/cơ quan hoặc quyền của bạn; dùng bản xuất ICS đầy đủ nếu được phép |
| Nhập xong không có sự kiện | Kiểm tra tuần đang xem, trạng thái hiển thị và ngày trong file; lịch học kỳ cũ có thể trống ở tuần này |
| Sự kiện bị trùng | Ẩn/xóa nguồn cũ hoặc dùng Replace .ics thay vì thêm một bản nữa |
| Giờ có vẻ sai | Kiểm tra múi giờ Windows, chi tiết sự kiện và bảo đảm đang dùng bản 0.2.3 trở lên |
| Đóng mà app vẫn chạy | Có thể đang bật Close to system tray; chọn Exit ở khay để thoát hẳn |
| Desktop còn ảnh lịch cũ | Exit, Refresh desktop và mở bản hiện tại; app không sửa file wallpaper |
| Không dùng được Desktop mode | Chuyển Floating; Explorer/WorkerW hoặc phần mềm wallpaper có thể cản việc gắn widget |
| Build báo không ghi đè được EXE | Thoát instance đang chạy từ thư mục đó rồi build lại |

## Build từ mã nguồn

Cần **CMake 3.24+** và **MinGW-w64 GCC + Ninja**, hoặc **Visual Studio 2022 C++ tools**. Không cần Python để build app.

```powershell
.\scripts\build.ps1
```

Script tạo Release build ở `build/release`, biên dịch và chạy CTest. Có thể chạy rõ từng lệnh với MinGW/Ninja:

```powershell
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

Cấu hình MSVC đã có nhưng chưa được kiểm chứng trên máy này:

```powershell
cmake -S . -B build/msvc -G "Visual Studio 17 2022" -A x64
cmake --build build/msvc --config Release
ctest --test-dir build/msvc -C Release --output-on-failure
```

### Đóng gói để chia sẻ

```powershell
.\scripts\package.ps1
```

| Kết quả | Mục đích |
| --- | --- |
| `dist/CalendarDeck/` | App chạy được, hướng dẫn và script shortcut |
| `dist/CalendarDeck-v0.2.4-windows-x64.zip` | Gói chương trình cho GitHub Releases |
| `dist/CalendarDeck-v0.2.4-source.zip` | Mã nguồn chọn lọc để chia sẻ/tải lên |
| `dist/SHA256SUMS.txt` | Mã kiểm tra SHA-256 của hai ZIP |

Gói source loại các bản build, EXE, lịch cá nhân, cache, bản sao lưu cục bộ và công cụ Codex. Quy tắc ignore cũng loại chúng khi dùng Git thông thường. Đưa ZIP chương trình vào **Releases**, không commit vào mã nguồn. Kiểm tra `git status` trước khi commit.

## Cấu trúc dự án

| Thư mục | Nội dung |
| --- | --- |
| `src/` | UI, xử lý lịch và lưu trữ |
| `resources/` | PNG gốc, ICO nhiều kích thước, manifest và thông tin phiên bản |
| `scripts/` | Build, đóng gói và tạo shortcut |
| `tests/` | Kiểm thử bằng dữ liệu tổng hợp |
| `docs/` | Kiến trúc và kết quả kiểm chứng |
| `build/`, `dist/` | File sinh tự động, không đưa vào Git |
| `input/`, `local-data/` | File cá nhân/bản lưu cục bộ, không đưa vào Git |

## Phạm vi, hiệu năng và kiểm chứng

App dùng message loop Win32 theo sự kiện và một worker nền, không chạy vòng vẽ liên tục 60 FPS. Một lần đo demo ngắn ghi nhận khoảng **48–49 MiB working set**. Đây không phải cam kết RAM cho mọi nguồn lịch, GPU hoặc driver.

Bộ kiểm thử core hiện có **96 kiểm tra**. Bản sửa giờ lịch lặp còn được đối chiếu với 5 file Google qua 104 tuần; 487 lượt sự kiện khớp với bộ tính độc lập. Xem [kết quả và phần chưa kiểm chứng](docs/VALIDATION.md).

Hỗ trợ DAILY/WEEKLY/MONTHLY/YEARLY, INTERVAL, COUNT, UNTIL, WKST, BYDAY, BYMONTHDAY, BYMONTH, BYSETPOS, EXDATE, RDATE và ngoại lệ RECURRENCE-ID. Chưa triển khai đầy đủ RFC 5545: VTIMEZONE tùy chỉnh, lịch lặp theo giờ, RDATE dạng PERIOD và RECURRENCE-ID RANGE chưa được hỗ trợ. Giới hạn gồm 100 nguồn, 10 MiB/feed, 50.000 VEVENT/feed và 10.000 lượt sự kiện/nguồn/tuần.

Bản này chưa có đăng nhập Google OAuth, tạo/sửa sự kiện, nhắc việc, Light Mode, chỉnh opacity hay chuyển ngôn ngữ giao diện. Lưới lịch chưa cung cấp UI Automation cho từng sự kiện. Đồng bộ Google thực tế và hành vi Desktop/DPI vẫn cần kiểm thử thủ công rộng hơn.

## Đóng góp và tài liệu tham khảo

Xem [CONTRIBUTING.md](CONTRIBUTING.md) và [ghi chú kiến trúc](docs/ARCHITECTURE.md).

Tài liệu: [Google iCal](https://support.google.com/calendar/answer/37648), [xuất lịch Google](https://support.google.com/calendar/answer/37111), [lịch công khai](https://support.google.com/calendar/answer/37083) và [RFC 5545](https://www.rfc-editor.org/rfc/rfc5545).
