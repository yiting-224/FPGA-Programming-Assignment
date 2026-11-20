// Generates report_zh.docx (Traditional Chinese report) with the `docx` npm package.
// Usage: node report/make_docx.js [output.docx]
//
// Inline markup used in the text below:
//   **bold**   *italic*   `code`   _[subscript]   ^[superscript]
const fs = require('fs');
const path = require('path');
const {
  Document, Packer, Paragraph, TextRun, HeadingLevel, AlignmentType, Table, TableRow, TableCell,
  WidthType, ShadingType, BorderStyle, LevelFormat, Footer, PageNumber,
} = require('docx');

const STUDENT_ID = '115062556';
const out = path.resolve(process.argv[2] || path.join(__dirname, '..', 'report_zh.docx'));

const FONT = { ascii: 'Times New Roman', hAnsi: 'Times New Roman', eastAsia: 'Microsoft JhengHei', cs: 'Times New Roman' };
const MONO = { ascii: 'Consolas', hAnsi: 'Consolas', eastAsia: 'Microsoft JhengHei', cs: 'Consolas' };
const BODY = 21;   // half-points (10.5 pt)
const SMALL = 18;  // 9 pt

// Parses the inline markup into TextRuns.
function runs(text, base = {}) {
  const res = [];
  const re = /(\*\*[^*]+\*\*|\*[^*]+\*|`[^`]+`|_\[[^\]]*\]|\^\[[^\]]*\])/g;
  let last = 0, m;
  const push = (t, o = {}) => { if (t) res.push(new TextRun({ text: t, font: FONT, size: BODY, ...base, ...o })); };
  while ((m = re.exec(text)) !== null) {
    push(text.slice(last, m.index));
    const tok = m[0];
    if (tok.startsWith('**')) res.push(...runs(tok.slice(2, -2), { ...base, bold: true }));
    else if (tok.startsWith('*')) res.push(...runs(tok.slice(1, -1), { ...base, italics: true }));
    else if (tok.startsWith('`')) push(tok.slice(1, -1), { font: MONO, size: (base.size || BODY) - 2 });
    else if (tok.startsWith('_[')) push(tok.slice(2, -1), { subScript: true });
    else push(tok.slice(2, -1), { superScript: true });
    last = m.index + tok.length;
  }
  push(text.slice(last));
  return res;
}

const P = (text, opts = {}) => new Paragraph({
  children: runs(text, opts.run || {}),
  alignment: opts.align || AlignmentType.JUSTIFIED,
  spacing: { after: 80, line: 300 },
  ...(opts.para || {}),
});
const H1 = (t) => new Paragraph({ heading: HeadingLevel.HEADING_1, children: [new TextRun({ text: t, font: FONT })] });
const H2 = (t) => new Paragraph({ heading: HeadingLevel.HEADING_2, children: [new TextRun({ text: t, font: FONT })] });
const bullet = (t) => new Paragraph({ numbering: { reference: 'bullets', level: 0 }, children: runs(t), spacing: { after: 40, line: 300 } });
let listId = 0;
const numbered = (items) => {
  const ref = 'num' + (listId++);
  numberingConfigs.push({
    reference: ref,
    levels: [{ level: 0, format: LevelFormat.DECIMAL, text: '%1.', alignment: AlignmentType.LEFT,
      style: { paragraph: { indent: { left: 420, hanging: 300 } } } }],
  });
  return items.map((t) => new Paragraph({ numbering: { reference: ref, level: 0 }, children: runs(t), spacing: { after: 40, line: 300 } }));
};
const numberingConfigs = [{
  reference: 'bullets',
  levels: [{ level: 0, format: LevelFormat.BULLET, text: '•', alignment: AlignmentType.LEFT,
    style: { paragraph: { indent: { left: 420, hanging: 300 } } } }],
}];

// Table helper. widths in DXA; first column left-aligned, others right-aligned.
const border = { style: BorderStyle.SINGLE, size: 4, color: '888888' };
const borders = { top: border, bottom: border, left: border, right: border };
function table(widths, rows, header = true) {
  const total = widths.reduce((a, b) => a + b, 0);
  return new Table({
    width: { size: total, type: WidthType.DXA },
    columnWidths: widths,
    rows: rows.map((row, ri) => new TableRow({
      tableHeader: header && ri === 0,
      children: row.map((cell, ci) => {
        const span = typeof cell === 'object' && cell.span ? cell.span : 1;
        const text = typeof cell === 'object' ? cell.text : String(cell);
        const w = widths.slice(ci, ci + span).reduce((a, b) => a + b, 0);
        const isHead = header && ri === 0;
        return new TableCell({
          borders,
          columnSpan: span,
          width: { size: w, type: WidthType.DXA },
          shading: isHead ? { fill: 'EEEEEE', type: ShadingType.CLEAR, color: 'auto' } : undefined,
          margins: { top: 40, bottom: 40, left: 90, right: 90 },
          children: [new Paragraph({
            alignment: (typeof cell === 'object' && cell.align) || (ci === 0 ? AlignmentType.LEFT : AlignmentType.RIGHT),
            children: runs(text, { size: SMALL, bold: isHead || (typeof cell === 'object' && cell.bold) }),
          })],
        });
      }),
    })),
  });
}
const note = (t) => P(t, { run: { size: SMALL, color: '333333' }, align: AlignmentType.LEFT });
const formula = (t) => new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 60, after: 100 }, children: runs(t, { italics: true }) });

const B = (s) => ({ text: s, bold: true });

const children = [
  new Paragraph({ heading: HeadingLevel.TITLE, children: [new TextRun({ text: '拓撲感知多 FPGA 分割（Topology-Aware Multi-FPGA Partitioning）', font: FONT })] }),
  P(`CS 516000 程式作業報告　　學號：${STUDENT_ID}`, { run: { color: '444444' }, align: AlignmentType.LEFT, para: { spacing: { after: 200 } } }),

  H1('1. 概述'),
  P('本分割器將有向超圖（hypergraph）的每個節點指派到 *K* 顆 FPGA 之一，目標是在滿足節點容量、I/O 上限與連線限制（每個訊號的所有 sink 所在 FPGA 都必須與其 source 所在 FPGA 直接相連）的前提下，使總連線成本（TLC）最小。程式以 C++17 撰寫，只使用標準函式庫（包含 `std::thread`），以 `make` 編譯。'),
  P('整體方法結合三個概念：'),
  ...numbered([
    '**多階層分割**（類似 hMETIS / KaHyPar [1,2]）：先以分群方式將超圖逐層粗化，在最粗層求出分割，再逐層投影回細層並以貪婪法改良。',
    '**多階層模擬退火** [3,4]：以上述分割為基礎，建立「保留分割」的階層，從最粗層到最細層逐層做退火。在粗層上一次移動的是整個群集，因此退火能把整個社群搬到另一顆 FPGA，這是單一節點移動做不到的。',
    '**平行多重起始**：最多四個工作執行緒以不同亂數種子與不同退火參數各自獨立嘗試，最後輸出最佳的合法解。',
  ]),
  P('所有成本與限制的計算都是精確且增量式的，評估一次移動的時間與被移動節點所連接的 net 數量成正比。'),

  H1('2. 目標函數與限制處理'),
  P('對於權重為 *w(e)*、source 位於 FPGA *s* 的 net *e*，令 *O(e)* 為至少放有一個 sink 的 FPGA 集合，則其成本為 *w(e)·Σ_[f∈O(e)\\{s}] D(s,f)*；若 *O(e)\\{s}* 非空，該 net 會在 *s* 以及 *O(e)\\{s}* 中每顆 FPGA 上各使用 *w(e)* 個 I/O 腳位。最佳化的目標為最小化'),
  formula('J = Σ_[e] w(e)·Σ_[f∈O(e)\\{s}] D(s,f) + λ·Σ_[f] max(0, IO(f) − IOlimit(f)) + μ·Σ_[e] w(e)·#{e 中位於與 s 無連線之 FPGA 上的 sink}'),
  P('其中 *D(a,b)* 為連線成本；對於**沒有連線**的 FPGA 對，*D* 設為很大的懲罰值 *B* = max(10000, 1000·最大連線成本)，λ 也取 *B*。最後一項（μ = 20·最大連線成本）計算每個被放在無連線 FPGA 上的 sink。若沒有這一項，當某個 net 有 20 個 sink 都在同一顆無連線 FPGA 上時，必須 20 個全部移走違規才會消失，單一移動看不到任何改善；加入此項後，每移走一個 sink 都會得到回饋。各限制的處理方式如下：'),
  bullet('**容量**（兩種節點類型）為*硬性*限制：只有目標 FPGA 有空間時才會執行移動；若空間不足，退火器改為嘗試*交換*（見 4.2 節）。'),
  bullet('**連線與 I/O** 限制在最佳化過程中為軟性限制，其懲罰大於任何可能的連線成本節省，因此區域搜尋會優先消除違規。比較解的好壞時採用字典序 *(跨越無連線 FPGA 的訊號數 + I/O 超量, TLC)*，因此合法解一定優先。'),
  bullet('**指派**：依建構方式，每個節點恰好被指派到一顆 FPGA。'),
  P('讀檔時會移除與 source 相同的 sink 以及重複的 sink，因為它們不會影響成本或 I/O 使用量。'),

  H1('3. 資料結構與增量評估'),
  P('超圖以 CSR 格式儲存：每個 net 存其權重、source 與 sink 清單；每個節點存其相連的 net 清單。分割狀態（`Refiner`）為每個 net 記錄其在每顆 FPGA 上的 sink 數量。sink 數不超過 8 的 net 使用稀疏的 *(FPGA, 數量)* 配對清單（最多 min(#sinks, K) 筆），較大的 net 使用長度為 *K* 的密集陣列，並明確維護 *|O(e)|*。此外狀態也記錄每顆 FPGA 的 I/O 使用量與兩種節點負載、目前成本、跨越無連線 FPGA 的訊號量，以及 I/O 總超量。'),
  P('將節點 *v* 從 FPGA *a* 移到 *b* 時，逐一評估每個相連的 net：'),
  bullet('**v 是 net e 的 sink**（source 在 *s*）：若 *a* 上的數量降為 0（且 *a≠s*），則 *a* 離開 *O(e)\\{s}*；若 *b* 原本數量為 0（且 *b≠s*），則 *b* 加入。成本依情況改變 *w·(D(s,b) − D(s,a))*；I/O 在 *a*、*b* 上 ±*w*，若 net 在被切割與未切割之間轉換，*s* 上也會改變。對密集 net 為 O(1)。'),
  bullet('**v 是 net e 的 source**：sink 數量不變；成本由 *w·Σ_[f∈O(e)] D(a,f)* 變為 *w·Σ_[f∈O(e)] D(b,f)*，並更新 *a*、*b*（若 net 切割狀態改變，則還有 *O(e)*）的 I/O 使用量。工作量為 O(|O(e)|)。'),
  P('I/O 的變化量累積在一個小型暫存陣列中，再據以計算超量項的變化。「只評估」與「評估並執行」共用同一個模板函式，保證評估與更新一致。我們以隨機測試驗證：執行數十萬次隨機移動後，增量狀態與完整重算的結果完全相同。'),

  H1('4. 演算法'),
  H2('4.1 多階層分割'),
  P('**粗化。**以隨機順序拜訪節點；尚未分群的節點 *u* 加入使「含權重懲罰的 heavy-edge 評分」[2] *r(u,C) = Σ_[e∋u,C] (w(e)/(|e|−1)) / (size(u)·size(C))* 最大的鄰近群集 *C*；超過 64 個 pin 的 net 不列入評分（例如 public5 中有 594 167 個 sink 的類時脈 net）。每個群集最多只能包含最小 type-1 容量與最小 type-2 容量的 6 %，使粗化後的問題仍容易裝箱。收縮時會丟棄落在 source 所在群集內的 sink、合併重複的 sink、移除空的 net，並將完全相同（source 與 sink 集合皆相同）的 net 合併且權重相加——這能精確保留任何分割的 TLC 與 I/O 使用量。當節點數降到 24·K 以下，或某一層縮減不到 3 % 時停止粗化。'),
  P('**初始分割。**在最粗層產生 12 個候選解並保留最佳者：(a) 拓撲感知的貪婪成長——依 BFS 順序拜訪節點，把每個節點放進「仍有空間且與已放置鄰居之連線成本最小」的 FPGA（同分時選剩餘空間最多者），如此會長出區域，並把溢出的部分放到連線便宜的 FPGA；(b) 隨機且滿足容量的指派。每個候選解再以嘗試所有 FPGA 的貪婪 K 路改良法改善。'),
  P('**反粗化。**將分割逐層投影回去；每一層執行數次貪婪改良：以隨機順序拜訪邊界節點（至少有一個被切割的相連 net 的節點），若移到「含有其鄰居且容量足夠」的最佳 FPGA 能嚴格降低 *J*，就執行移動。'),
  H2('4.2 多階層模擬退火'),
  P('貪婪改良很快就會卡住，主要原因是 FPGA 被填滿。因此主要的最佳化器是嵌入多階層架構 [4] 的模擬退火 [3]：'),
  ...numbered([
    '以目前的分割建立新的階層，並限制只有同一顆 FPGA 內的節點可以分群。因此每一層都代表完全相同的分割，最粗層最多 *N_[top]* 個節點（預設 5000；其他嘗試也使用 n/3 與 n/8，見 4.3 節）。',
    '從最粗層到最細層依序退火。第 *l* 層分到的剩餘時間比例與 *n_[l]^[−1]* 成正比，也就是粗層分到大部分時間。初始溫度為 *t_[0] = τ·（隨機移動之正 Δ 的平均）*，最粗層 *τ = τ_[top]*，往下每層乘以 0.05（粗層高溫、細層接近貪婪）；溫度隨時間呈幾何遞減至 0.05，最後以一次貪婪掃描收尾。',
    '**移動。**均勻抽取一個邊界節點 *v*（每次執行約重建邊界清單 100 次）。目標 *b* 是隨機一顆含有 *v* 鄰居的 FPGA；以 1/16 的機率改為任選一顆 FPGA，使空的或較遠的 FPGA 也能被使用。若 *b* 沒有空間，則嘗試與 *b* 中一個能放進 *a* 的隨機邊界節點 *u* **交換**（每顆 FPGA 各自維護邊界節點池，使此步驟為 O(1)）。對精確的合併變化量套用 Metropolis 準則。',
    '最佳狀態（依第 2 節的字典序）以延遲方式追蹤：只記錄自上次出現新最佳解以來被移動的節點，因此儲存新最佳解的時間與兩次最佳解之間的移動次數成正比。',
  ]),
  P('**修復。**若一次嘗試結束時仍有違規，會進入修復階段：拜訪相關節點（位於無連線 FPGA 上的 sink 及其 source、I/O 超量 FPGA 上的邊界節點），嘗試所有 FPGA，若目標已滿則與抽樣節點交換，並執行所有能降低 *J* 的移動。'),
  P('交換移動被證實非常關鍵：在 public1 中約 98 % 的單一移動會被容量擋下，光是加入交換就讓退火後的 TLC 從 1377 降到 238。'),
  H2('4.3 平行多重起始與時間管理'),
  P('預設時間預算為 300 秒上限中的 250 秒（再扣除輸出所需的餘裕）。*W* = min(4, 核心數) 個工作執行緒（輸入非常大時會減少以控制記憶體）重複執行*嘗試* = 多階層分割 + 多階層退火。每次嘗試的長度為 max(3 秒, 2 毫秒·N)：小型輸入（N = 600）每個執行緒可重新開始數十次，節點數 ≥ 125 000 的輸入則只做一次長時間嘗試。嘗試之間以 τ_[top] ∈ {2, 1, 4, 0.5} 與 *N_[top]* ∈ {5000, n/3, n/8} 增加多樣性：沒有任何單一設定在所有測資上都最好（例如 public2 雖然只有 1600 個節點仍需要粗層退火，而 public1 不粗化反而最好）。執行緒之間只共用最佳解（以 mutex 保護），超圖為唯讀。由於嘗試受時間限制且平行執行，每次執行結果會略有差異。'),

  H1('5. 結果'),
  P('實驗環境：4 核心 Linux 容器、g++ 13.3、`-O3`。TLC 皆經助教提供的 checker 驗證（所有解皆合法）。'),
  table([850, 950, 450, 950, 650, 600, 850, 1400, 1000, 1150], [
    ['測資', 'N', 'K', 'M', 'L', '合法', 'TLC', '初始 ML TLC', '執行時間', '記憶體峰值'],
    ['public1', '600', '8', '1 239', '25', '是', B('175'), '2 368', '248 s', '13 MB'],
    ['public2', '1 600', '4', '2 157', '6', '是', B('246'), '561', '248 s', '34 MB'],
    ['public3', '25 000', '32', '27 482', '422', '是', B('8 654'), '18 211', '248 s', '29 MB'],
    ['public4', '40 000', '32', '45 504', '418', '是', B('9 192'), '17 316', '248 s', '36 MB'],
    ['public5', '999 999', '64', '953 817', '1 881', '是', B('42 184'), '93 949', '247 s', '684 MB'],
  ]),
  note('「初始 ML TLC」為單次多階層分割加貪婪改良（一次嘗試的第一階段）的結果，供比較用。使用預設時間預算（250 秒）、4 個執行緒。由於搜尋具隨機性且為多執行緒，重複執行的結果會相差數個百分點（例如 public4 兩次完整執行分別為 8 783 與 9 192）。'),
  H2('開發歷程（消融比較）'),
  P('下表顯示開發過程中各元件對結果的影響（單次執行，括號內為時間上限；因退火具隨機性，數字僅供參考）。'),
  table([5500, 1200, 1200, 1300], [
    ['版本', 'public1', 'public3', 'public5'],
    ['多階層分割 + 貪婪改良', '2368', '18211', '93949'],
    ['+ V-cycle、單層退火（無交換）（20 s / 20 s / 90 s）', '1377', '14567', '89162'],
    ['+ 退火加入交換移動（20 s / 60 s / 90 s）', '238', '11636', '86809'],
    ['+ 先在一個粗層退火，再於最細層退火（60 s / 90 s）', '–', '9643', '71401'],
    ['+ 多階層退火、時間偏重粗層、較高溫（60 s / 120 s）', '–', '9506', '52106'],
    ['+ 平行多重起始、多樣化參數（最終版）', { text: '見上表', span: 3, align: AlignmentType.CENTER }],
  ]),
  H2('穩健性測試'),
  P('除了公開測資外，程式也測試了：(i) 作業說明中的範例；(ii) K = 1 的測資；(iii) 只保留我們某個解所用到的 166 條連線的 public3（合法，40 秒內 TLC 10384）；(iv) 同上，再把所有 I/O 上限降到該解的最大 I/O 使用量（775）：此測資只有極少數放置方式是合法的；加入逐 sink 懲罰項之前，程式卡在 2 個違規，加入之後 4 個執行緒中有 3 個找到合法解。**限制：**在 4×8 網格拓撲（每顆 FPGA 只與上下左右相連）搭配 public3 的電路時找不到合法解；sink 多達 99 個的 net 必須放進一顆 FPGA 及其最多四個鄰居之中，此測資本身是否有解並不清楚。'),

  H1('6. 複雜度'),
  P('令 *P* 為 pin 的總數。讀檔、一層粗化與收縮的時間為 O(P log P)（排序 sink 清單與 net 雜湊值）；pin 計數器的記憶體最差為 O(P + mK)（只有 sink 數超過 8 的 net 使用 K 個計數器）。評估一次移動的成本為 O(Σ_[e∋v] c(e))，其中 *v* 為 sink 時 c(e) = 1，為 source 時 c(e) = |O(e)| ≤ K。最大公開測資的記憶體峰值列於上表，遠低於 8 GB 上限。'),

  H1('7. 編譯與使用'),
  new Paragraph({ shading: { fill: 'F4F4F4', type: ShadingType.CLEAR, color: 'auto' }, spacing: { before: 60 }, children: [new TextRun({ text: 'make', font: MONO, size: 19 })] }),
  new Paragraph({ shading: { fill: 'F4F4F4', type: ShadingType.CLEAR, color: 'auto' }, spacing: { after: 100 }, children: [new TextRun({ text: './partitioner <input_file> <output_file> [time_limit_seconds]', font: MONO, size: 19 })] }),
  note('可選的環境變數（僅供實驗用）：`PARTITIONER_TIME_LIMIT`（預設 250）、`PARTITIONER_THREADS`、`PARTITIONER_SEED`、`PARTITIONER_VERBOSE`。原始檔：`hypergraph.*`（資料結構、讀檔、收縮）、`coarsen.cpp`（分群）、`initial.cpp`（初始分割、階層、多階層流程）、`refiner.*`（增量狀態、貪婪改良、模擬退火）、`main.cpp`（平行多重起始）、`util.hpp`（計時器、亂數產生器）。'),

  H1('參考文獻'),
  ...[
    'G. Karypis, R. Aggarwal, V. Kumar, S. Shekhar. Multilevel hypergraph partitioning: applications in VLSI domain. *IEEE Trans. VLSI Systems* 7(1), 1999.',
    'S. Schlag, V. Henne, T. Heuer, H. Meyerhenke, P. Sanders, C. Schulz. k-way hypergraph partitioning via n-level recursive bisection. *ALENEX* 2016（KaHyPar 所用、含節點權重懲罰的 heavy-edge 評分）。',
    'S. Kirkpatrick, C. D. Gelatt, M. P. Vecchi. Optimization by simulated annealing. *Science* 220, 1983.',
    'C. Walshaw. Multilevel refinement for combinatorial optimisation problems. *Annals of Operations Research* 131, 2004.',
    'C. M. Fiduccia, R. M. Mattheyses. A linear-time heuristic for improving network partitions. *DAC* 1982（以增益為基礎的移動改良）。',
    'D. Blackman, S. Vigna. Scrambled linear pseudorandom number generators. *ACM TOMS* 47(4), 2021（本程式使用的 xoshiro256** 亂數產生器）。',
  ].map((t, i) => new Paragraph({ spacing: { after: 40 }, indent: { left: 420, hanging: 420 },
    children: [new TextRun({ text: `[${i + 1}] `, font: FONT, size: SMALL }), ...runs(t, { size: SMALL })] })),
  note('所有程式碼皆為本作業自行撰寫，未使用任何外部分割程式碼或函式庫。'),
];

const heading = (size) => ({ run: { font: FONT, size, bold: true, color: '111111' } });
const doc = new Document({
  creator: STUDENT_ID,
  title: '拓撲感知多 FPGA 分割 — 報告',
  styles: {
    default: { document: { run: { font: FONT, size: BODY } } },
    paragraphStyles: [
      { id: 'Title', name: 'Title', basedOn: 'Normal', next: 'Normal', ...heading(32),
        paragraph: { spacing: { before: 0, after: 60 }, keepNext: true } },
      { id: 'Heading1', name: 'Heading 1', basedOn: 'Normal', next: 'Normal', quickFormat: true, ...heading(25),
        paragraph: { spacing: { before: 240, after: 80 }, keepNext: true, outlineLevel: 0,
          border: { bottom: { style: BorderStyle.SINGLE, size: 6, color: '999999', space: 1 } } } },
      { id: 'Heading2', name: 'Heading 2', basedOn: 'Normal', next: 'Normal', quickFormat: true, ...heading(22),
        paragraph: { spacing: { before: 160, after: 60 }, keepNext: true, outlineLevel: 1 } },
    ],
  },
  numbering: { config: numberingConfigs },
  sections: [{
    properties: { page: { margin: { top: 1000, bottom: 1000, left: 1000, right: 1000 } } },
    footers: { default: new Footer({ children: [new Paragraph({ alignment: AlignmentType.CENTER,
      children: [new TextRun({ children: [PageNumber.CURRENT], font: FONT, size: SMALL })] })] }) },
    children,
  }],
});

Packer.toBuffer(doc).then((buf) => {
  fs.writeFileSync(out, buf);
  console.log('wrote', out);
});
