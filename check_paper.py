import fitz

doc = fitz.open('2504.17033v2.pdf')
print(f"Total pages: {len(doc)}")

print("\n--- Mục lục và tiêu đề các Section ---")
for page_num in range(len(doc)):
    page = doc[page_num]
    text = page.get_text()
    lines = text.split('\n')
    for line in lines:
        line_s = line.strip()
        if any(line_s.startswith(f"{i} ") or line_s.startswith(f"{i}. ") for i in range(1, 15)):
            if len(line_s) < 80:
                print(f"Page {page_num + 1}: {line_s}")

print("\n--- Tìm kiếm bảng biểu / biểu đồ / từ khóa thực nghiệm ---")
keywords = ['experiment', 'empirical', 'benchmark', 'implementation', 'code', 'github', 'cpu', 'seconds', 'milliseconds', 'runtime', 'table 1', 'table 2', 'figure 1', 'figure 2']
matches = {kw: [] for kw in keywords}

for page_num in range(len(doc)):
    page = doc[page_num]
    text = page.get_text()
    text_lower = text.lower()
    for kw in keywords:
        if kw in text_lower:
            matches[kw].append(page_num + 1)

for kw, pages in matches.items():
    if pages:
        print(f"Keyword '{kw}' xuất hiện tại trang: {pages}")
    else:
        print(f"Keyword '{kw}': KHÔNG TÌM THẤY")
