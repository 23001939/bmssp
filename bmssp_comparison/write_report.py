"""Generate reviewable report from measured results, without rerunning benchmarks."""
import json
import os
from run_comparison import ROOT, git_commit

p=ROOT/'results.json'
d=json.loads(p.read_text())
d['commits']={repo.name:git_commit(repo) for repo in (ROOT/'repos').iterdir()}
d['processor_identifier']=os.environ.get('PROCESSOR_IDENTIFIER')
d['cpu_threads']=os.cpu_count()
d['compiler']='g++ 16.2.0, -std=c++20 -O3 -DNDEBUG'
p.write_text(json.dumps(d,indent=2),encoding='utf-8')

names={
 'cpp_dijkstra':'lcs147: Dijkstra C++ (đối chứng)',
 'cpp_wc_bounded':'lcs147: BMSSP WC, không chuyển đổi bậc',
 'cpp_expected_bounded':'lcs147: BMSSP EXPECTED, không chuyển đổi bậc',
 'cpp_wc':'lcs147: BMSSP WC, có chuyển đổi bậc',
 'cpp_expected':'lcs147: BMSSP EXPECTED, có chuyển đổi bậc',
 'python_dijkstra':'Dijkstra Python đối chứng của bộ đo',
 'nobelk':'nobelk: bản Python tham chiếu',
 'bzantium_v1':'bzantium: Python V1',
 'bzantium_v2':'bzantium: Python V2',
 'logtwothirds_reference':'logtwothirds: Python tham chiếu, KHÔNG phải Rust',
}
lines=[
 '**So sánh các repo BMSSP — đo tại máy ngày 13/11/2026**', '',
 'Kết luận trong phạm vi đã đo: `lcs147/bmssp` là repo BMSSP nhanh nhất trong các bản chạy được. Trên đồ thị ngẫu nhiên có trọng số thực và bậc hằng số, 50.000 đỉnh và 199.996 cạnh, bản EXPECTED mất 63,301 ms và bản WC mất 69,428 ms. Dijkstra C++ trong cùng repo mất 16,188 ms.', '',
 'Chưa có cơ sở xếp hạng tuyệt đối cả sáu repo: Rust, Go và .NET 10 SDK không có trong môi trường này. Python tham chiếu của logtwothirds đã được chạy riêng; thời gian của nó không đại diện cho lõi Rust.', '',
 '**Cách đo**', '',
 '- Mã nguồn upstream giữ nguyên. Chỉ thêm wrapper benchmark bên ngoài các repo.',
 '- C++: g++ 16.2.0, `-std=c++20 -O3 -DNDEBUG`. Python: '+d['python'].splitlines()[0]+'.',
 '- Máy: '+str(d['platform'])+'; '+str(d['processor_identifier'])+'; '+str(d['cpu_threads'])+' luồng CPU khả dụng.',
 '- Chạy tuần tự trên cùng dữ liệu, source=0. Không tính đọc file, sinh đồ thị, import module và kiểm tra kết quả.',
 '- C++ dùng trung vị 5 lần sau 1 lần làm nóng; Python trung vị 3 lần sau 1 lần kiểm tra/làm nóng. Không so sánh các chênh lệch nhỏ như một kết luận chắc chắn.',
 '- Thời gian bảng chính gồm khởi tạo solver và prepare_graph nếu có. Mảng kề đầu vào được tạo trước thời gian đo; C++ constructor có copy mảng kề. JSON giữ cả thời gian execute riêng của C++.',
 '- Đồ thị ngẫu nhiên gồm đường backbone bảo đảm tới được mọi đỉnh và ba lớp hoán vị ngẫu nhiên. Bậc vào tối đa 4, bậc ra tối đa 5. Vì đã có bậc hằng số, prepare_graph(false) là chế độ phù hợp; prepare_graph(true) đo thêm để thấy chi phí chuyển đổi.',
 '- Các bản source-to-target được chạy với một sink xa hơn mọi đỉnh khác, buộc chúng xử lý mọi đỉnh cần so sánh. Khoảng cách toàn bộ đỉnh được kiểm tra, không chỉ đỉnh đích.',
 '- Oracle: Dijkstra độc lập bằng Python heapq; sai số rel_tol=abs_tol=1e-9. Đây là kiểm tra thực nghiệm trên hữu hạn đồ thị, không phải chứng minh tổng quát.',
 '- Dữ liệu nhỏ và vừa, chủ yếu đồ thị tổng hợp; kết quả không bảo đảm thứ hạng trên đồ thị thực, máy khác hay mọi kích thước.', '',
 '**Kết quả đầy đủ, đơn vị mili giây; thấp hơn là nhanh hơn**', '',
 '| Bản triển khai | '+' | '.join(c['name'] for c in d['cases'])+' |',
 '|---|'+'---:|'*len(d['cases']),
]
for mode,name in names.items():
    cells=[]
    for c in d['cases']:
        r=c['algorithms'][mode]
        cells.append(f"{r['median_ms']:.3f}" if r['status']=='PASS' else r['status'])
    lines.append('| '+name+' | '+' | '.join(cells)+' |')
lines += ['',
 '**Các phát hiện ảnh hưởng đến việc chọn repo**','',
 '- Bzantium V2 sai trên cả ba đồ thị ngẫu nhiên trọng số thực (2.000, 10.000, 50.000 đỉnh) và ca phản ví dụ. Các ca nguyên và zero/ties đã chạy thì khớp; không được xếp nó là solver nhanh nhất cho trọng số thực không âm tổng quát.',
 '- Phản ví dụ API trực tiếp: 0→1 (0,9), 0→2 (0,1), 2→1 (0,1), 1→3 (0,1), 0→3 (2,0). Với Graph(1500) để vượt nhánh fallback n<1000, solve(0,3) trả khoảng cách 1,0 thay vì 0,3. Chỉ bốn đỉnh tham gia; các đỉnh còn lại cô lập. Chi tiết trong audit.json.',
 '- Trên ca real_10000, bzantium V1 gọi một base case bound=∞, xử lý toàn bộ 10.000 đỉnh. Nobelk cũng gọi một base case xử lý cả 10.000 đỉnh, trong khi k=3. Các số đo này không chứng minh hiệu năng thuật toán gốc có cutoff k+1.',
 '- Rust logtwothirds chưa được đo. Theo tài liệu upstream, bmssp-fast thay đổi thuật toán, còn auto chọn Dijkstra. Không gộp các chế độ này với BMSSP gốc khi xếp hạng.',
 '- C# chethandvg chưa chạy vì chỉ có .NET runtime 5.0.17, không có .NET 10 SDK. Go mfreeman451 chưa chạy vì không có Go; source singleton chuyển ngay sang thủ tục bucket/delta-stepping, khác Algorithm 3.', '',
 '**Nguồn công bố, không gộp vào bảng đo tại máy**', '',
 '- [logtwothirds BENCHMARKS](https://github.com/primaryaesthetics/logtwothirds/blob/main/BENCHMARKS.md): số liệu Rust do tác giả đo. [VARIANTS](https://github.com/primaryaesthetics/logtwothirds/blob/main/VARIANTS.md) giải thích thay đổi của bmssp-fast.',
 '- [C# benchmark-results](https://github.com/chethandvg/breaking-the-sorting-barrier/blob/main/results/benchmark-results.md): khác máy, khác đồ thị, không thể xếp chung với bảng tại máy.', '',
 '**Commit mã nguồn đã tải**', '',
 '| Thư mục | Repo | Commit |','|---|---|---|',
]
repos={'cpp':'lcs147/bmssp','rust_python':'primaryaesthetics/logtwothirds','python_reference':'nobelk/single-source-shortest-path','python_variants':'bzantium/bmssp-python','csharp':'chethandvg/breaking-the-sorting-barrier','go':'mfreeman451/bmssp-go'}
for name,sha in d['commits'].items():
    repo=repos[name]
    lines.append(f'| {name} | [{repo}](https://github.com/{repo}) | [{sha[:12]}](https://github.com/{repo}/tree/{sha}) |')
lines += ['', '**Tái lập**', '',
 'Từ C:\\ttss:', '', '```powershell',
 'g++ -std=c++20 -O3 -DNDEBUG bmssp_comparison/bench_cpp.cpp -o bmssp_comparison/bench_cpp.exe',
 'python -X utf8 bmssp_comparison/run_comparison.py',
 'python -X utf8 bmssp_comparison/audit_variants.py',
 'python -X utf8 bmssp_comparison/write_report.py', '```', '',
 'Kết quả thô: results.json. Lỗi tái lập: audit.json. Đồ thị và khoảng cách C++: data/. Các thời gian có thể dao động theo tải nền và bộ nhớ Windows.', '']
(ROOT/'REPORT.md').write_text('\n'.join(lines),encoding='utf-8')
print('Report saved:',ROOT/'REPORT.md')
print('Commits:',json.dumps(d['commits']))
