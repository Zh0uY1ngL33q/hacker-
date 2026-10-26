# 失败诊断：`sigreport.json`

当签名表与游戏版本不匹配时，那一版 DLL 除了列出失败项之外什么都不能提供，而
200 MB 的解密镜像又没法直接看。所以扫描失败时程序会自己产出两份东西：

| 文件 | 位置 | 用途 |
| --- | --- | --- |
| `Minecraft.Windows_<版本>_<时间>.image` | `RoamingState\Solstice\Dump\` | 解密后的完整镜像，离线改签名用 |
| `sigreport.json` | 同上 | 失败清单 + 近似命中（哪个签名的哪几个字节变了） |

`sigreport.json` 很小（几十 KB），只发它就能覆盖大部分修复工作。

## 它是怎么来的

`OffsetProvider` / `SigManager` 的每条签名在**没有精确命中**时，会把同一条模式
再扫一遍，但允许最多 6 个「固定字节」对不上（`CodeSection::fuzzyFind`）。
预算从 1 逐级放大到 6，用最小的那个能命中的预算，这样报出来的差异最短、最可信。

游戏跨小版本时函数形状基本不变，变的是立即数：成员偏移、call 相对位移、小常量。
所以「命中了、只是几个字节不同」通常意味着签名可以直接修好；「6 个字节以内都找不到」
才说明函数真的被重写或删掉了，那种必须看反汇编。

## 结构

```json
{
  "offsets": {
    "total": 67,
    "failed": ["Level_getRuntimeActorList", "..."],
    "ambiguous": [{ "name": "Actor_getStatusFlag", "matches": 3, "rva": "0x1A2B3C40" }]
  },
  "signatures": { "total": 118, "failed": ["..."], "ambiguous": ["..."] },
  "nearMisses": [
    {
      "name": "ItemStack_fromDescriptor",
      "pattern": "48 89 5C 24 ? 48 89 74 24 ? ...",
      "found": true,
      "rva": "0x2B1C0040",
      "actual": "48 89 5C 24 08 48 89 74 24 10 ...",
      "candidates": 1,
      "diffs": [{ "at": 5, "from": 63, "to": 8 }]
    }
  ]
}
```

- `rva` 是模块相对地址，和日志里的地址可以直接比对（镜像的每个字节都在相同的 RVA 上）。
- `diffs[].at` 是**模式里的下标**（从 0 开始，`?` 也占一位），`from` / `to` 是十六进制
  值。把 `pattern` 里对应的那个字节换成 `to` 就是新签名。
- `candidates > 1` 表示有多处同样好的候选 —— 和「精确匹配到多处」一样不可盲信，
  修的时候要加长上下文。
- `found: false` 表示 6 个字节以内找不到，需要反汇编重新定位。

## 用法

1. 打开 `sigreport.json`，先数三类各多少条：
   - `offsets.failed` / `signatures.failed` 里**在同一份 `nearMisses` 里有 `found: true`** 的
     → 直接按 `diffs` 改签名，不用看镜像；
   - `found: false` 的 → 必须在 `.image` 上反汇编；
   - `ambiguous` 的 → 地址虽然找到了，但可能取错，优先加长上下文。
2. 改完重编、重注入，看 `Failed to find N signatures/offsets!` 的 N 有没有下降。

## 关于 `ambiguous` 的数量

`E8 ? ? ? ?` 开头的那类签名（`SigType::RefSig`）本质上是「某个 call 点」，
在多处出现是正常的，`candidates`/`matches` 大不代表它坏了 —— 它只说明这条签名
本身不足以定位。真正要担心的是二义性出现在本来应该唯一的函数上。
