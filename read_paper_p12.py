import fitz

doc = fitz.open('2504.17033v2.pdf')
print("=== TRANG 1 ===")
print(doc[0].get_text())

print("\n=== TRANG 2 ===")
print(doc[1].get_text())
