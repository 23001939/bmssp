import fitz

doc = fitz.open('2504.17033v2.pdf')
print("=== TRANG 3 ===")
print(doc[2].get_text()[:2500])

print("\n=== TRANG 16 ===")
print(doc[15].get_text()[:2500])

print("\n=== TRANG 17 ===")
print(doc[16].get_text()[:2500])
