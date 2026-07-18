# Thesis LaTeX skeleton

Bộ file skeleton để paste vào template khóa luận ĐHKHTN VNU-HCM Khoa Điện tử - Viễn thông (https://github.com/anhnguyen-kelly23/Thesis_Template_latex).

## Cách sử dụng

1. Clone template LaTeX gốc từ link trên.
2. Copy các file trong thư mục này vào cấu trúc tương ứng của template:
   - `thesis/Content/chapter1.tex` → `Content/chapter1.tex`
   - ... tương tự chapter2-6.
   - `thesis/Appendix/tomtat.tex` → `Appendix/tomtat.tex`.
3. Cập nhật `main.tex` trong template gốc để include đủ 6 chapter (mặc định template chỉ có chapter1 + chapter2 sample).
4. Điền nội dung mỗi section theo hướng dẫn trong comment.

## Cấu trúc chương

Theo outline chi tiết trong [../docs/13-thesis-outline.md](../docs/13-thesis-outline.md):

- Chương 1: Giới thiệu
- Chương 2: Tổng quan nghiên cứu
- Chương 3: Cơ sở lý thuyết
- Chương 4: Triển khai hệ thống
- Chương 5: Kết quả thực nghiệm
- Chương 6: Kết luận

## Ghi chú

- Skeleton chỉ chứa cấu trúc `\chapter`, `\section`, `\subsection` và comment gợi ý nội dung.
- Không chứa hình ảnh, bảng, code snippet — cần bổ sung khi viết.
- Trích dẫn `\cite{...}` chưa có; thêm entry vào `References/references.bib` khi cần.
