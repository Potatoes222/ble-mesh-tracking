# Beacon — XIAO nRF52840 (Seeed Studio)

Firmware BLE beacon cho tag, ban danh cho board **Seeed XIAO nRF52840**.

Ban song song: [`../Beacon_ProMicroNrf52840`](../Beacon_ProMicroNrf52840) cho board
ProMicro/nice!nano v2. **Logic beacon/auth giong het nhau**, chi khac phan phu
thuoc phan cung (dia chi partition, cach doc pin, cau hinh nguon).

---

## 1. Build

```powershell
west build -b xiao_ble/nrf52840 -d build . --pristine
```

---

## 2. Flash — DOC KY PHAN NAY

Bootloader la **MCUboot** (verify chu ky ECDSA-P256). **KHONG duoc flash thang
file `zephyr.uf2` cua app** — do la ban **CHUA KY**, MCUboot se tu choi va board
khong bao gio boot len app.

Trieu chung khi flash nham file chua ky (rat de hieu nham la "board hong"):

- Drive `XIAO-SENSE` bien mat binh thuong sau khi copy (tuong nhu thanh cong)
- **Nhung khong co COM port cua app**, khong phat BLE, khong log gi ca
- Ly do khong co log: `mcuboot.conf` da tat console cua bootloader nen no tu
  choi app trong im lang

> Loi nay da lam mat nhieu gio debug ngay 24-25/07/2026. Da tung nghi oan cho:
> code timer, viec disable SPI2, build incremental, va ca phan cung board.

### Cach dung

```powershell
.\make_uf2.ps1
```

Script tao `tag_Xiao_SIGNED.uf2`. Sau do:

1. **Double-tap nut RESET** de vao bootloader.
2. Drive `XIAO-SENSE` hien len -> copy file `.uf2` vao.
3. Drive tu bien mat = board da reset va dang chay firmware.
4. Kiem tra: co COM port moi hien ra + nRF Connect thay `Test beacon`.

### Tu kiem tra file truoc khi flash

| Dau hieu | Dung | Sai |
|---|---|---|
| Kich thuoc | ~329 KB (gop) | ~270 KB (chi app, chua ky) |
| Dia chi block dau | `0x27000` (MCUboot) | `0x33000` (app) |

### Neu board khong boot / bootloader "lo do"

Neu drive khong bien mat sau khi copy, hoac double-tap khong an: **cat nguon
hoan toan** (rut USB **va** thao pin) ~10s roi cam lai. Reset am (double-tap)
khong xoa duoc trang thai ket cua bootloader; chi cold-boot moi xoa duoc.

---

## 3. File firmware da luu

| File | Noi dung |
|---|---|
| `_fw_backup/tag_WORKING_v5equiv_SIGNED.uf2` | **Ban an toan** — chay duoc chac chan (tuong duong v5, chua toi uu timer) |
| `tag_OPTIMIZED_SIGNED.uf2` | Ban toi uu (timer fix + tat SPI2) — da xac nhan boot OK |

Cac file `tag_v5rebuild_*.uf2` la ban trung gian luc debug, **khong dung de
flash** (ban `_app`/`_mcuboot` la file roi chua ky).

---

## 4. Trang thai phan cung (25/07/2026)

> **Board XIAO hien tai da HONG IC sac (BQ25101).** Trieu chung: den LED sac
> sang lien tuc sai ca khi chi cam pin lan chi cam USB, board tu nong len, do
> duoc dong ro bat thuong ~88 mA (binh thuong chi ~12-20 µA).
>
> Nguyen nhan: do dong tren duong BAT+ **trong khi USB van dang cam** — luc do
> IC sac dang bom dong sac qua dung duong dang do, gay qua tai lam hong die.
>
> **BAI HOC: luon rut USB truoc khi cam dong ho do dong vao duong pin.**
>
> Board nay khong nen cam nguon (ca pin lan USB) cho den khi thay duoc IC sac.
> Cong viec da chuyen sang board ProMicro.
