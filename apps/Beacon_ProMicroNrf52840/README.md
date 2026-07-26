# Beacon — ProMicro nRF52840 (nice!nano v2 compatible)

Firmware BLE beacon cho tag, ban danh cho board **ProMicro nRF52840** (vd "Nologo
ProMicro nRF52840" ban tren hshop) — tuong thich chuan nice!nano v2.

Ban song song: [`../Beacon_XiaoNrf52840`](../Beacon_XiaoNrf52840) cho board XIAO
nRF52840. **Logic beacon/auth giong het nhau**, chi khac phan phu thuoc phan cung
(dia chi partition, cach doc pin, cau hinh nguon).

---

## 1. Build

```powershell
west build -b promicro_nrf52840/nrf52840/uf2 -d build . --pristine
```

> Luu y board target phai co duoi **`/uf2`**. Bien the nay moi dung layout
> partition cho Adafruit UF2 bootloader (`nrf52840_partition_uf2_sdv6.dtsi`).

### Ban CHAY THAT khong co COM port — day la CO Y

Ban mac dinh da **go han USB + console + log** vi chung ton ~0.8mA lien tuc
(gan nua tong dong tieu thu) ke ca khi da rut day USB — xem muc 4.

Nghia la sau khi flash: **khong co COM port, khong co log**. Do la dung, khong
phai loi. Muon biet tag con song hay khong thi xem log ben **Scanner**
(`BMT_TAGTBL: New tag: 0x0001`), hoac quet bang nRF Connect thay `Test beacon`.

### Khi can xem log de debug

```powershell
west build -b promicro_nrf52840/nrf52840/uf2 -d build_debug . --pristine `
    -- "-DBeacon_ProMicroNrf52840_EXTRA_CONF_FILE=debug.conf"
```

`debug.conf` bat lai USB CDC-ACM console + log. **Dung dung ban nay de do dong**
(se cao hon ban that ~0.8mA).

> Tien to `Beacon_ProMicroNrf52840_` la bat buoc: day la build nhieu image kieu
> sysbuild, thieu tien to se bao loi "CMake configure failed for Zephyr project".

---

## 2. Flash — DOC KY PHAN NAY

Bootloader la **MCUboot** (co verify chu ky ECDSA-P256). Nghia la **KHONG duoc
flash file `zephyr.uf2` cua app**: file do la ban **CHUA KY**, MCUboot se tu choi
va board se khong bao gio boot len app (bieu hien: drive UF2 bien mat binh thuong
nhung **khong co COM port cua app**, khong phat BLE, khong log gi ca — vi
`mcuboot.conf` da tat console cua bootloader).

> Loi nay tung lam mat nhieu gio debug o ban XIAO: cu tuong code sai / board hong,
> thuc te chi la flash nham file chua ky.

### Cach dung: gop MCUboot + app DA KY roi convert sang UF2

Chay script co san:

```powershell
.\make_uf2.ps1
```

Script se tao `tag_ProMicro_SIGNED.uf2`. Sau do:

1. Dua board vao bootloader: **chap nhanh 2 lan** giua pad **RST** va **GND**
   (board nay khong co nut reset roi — khac XIAO).
2. Drive USB hien len -> copy file `.uf2` vao.
3. Drive tu bien mat = board da reset va dang chay firmware.

### Tu kiem tra file truoc khi flash

| Dau hieu | Dung | Sai |
|---|---|---|
| Kich thuoc | ~321 KB (gop) | ~270 KB (chi app, chua ky) |
| Dia chi block dau | `0x26000` (MCUboot) | `0x32000` (app) |

---

## 3. Khac biet so voi ban XIAO

| | XIAO nRF52840 | ProMicro nRF52840 |
|---|---|---|
| Bootloader Adafruit | SoftDevice s140 **v7** | SoftDevice s140 **v6** |
| MCUboot dat tai | `0x27000` | **`0x26000`** |
| App (slot0) tai | `0x33000` | **`0x32000`** |
| Vao bootloader | double-tap nut RESET | **chap RST-GND 2 lan** |
| Duong nguon | pin -> BQ25101 -> LDO ngoai -> VDD | **pin -> thang vao VDDH** |
| Doc dien ap pin | chia ap ngoai 1M/510k -> P0.31, phai keo P0.14 LOW | **VDDHDIV5 noi bo**, khong can GPIO |
| Bao dang sac | doc P0.17 | **khong co** (`is_charging()` luon false) |
| DC/DC | board tu bat san (reg0+reg1) | **chua bat** — xem `prj.conf` |

Chi tiet + ly do day du: doc comment trong
`boards/promicro_nrf52840_nrf52840_uf2.overlay`, `src/bmt_battery.c`, `prj.conf`.

---

## 4. Ket qua do dong tieu thu (25/07/2026)

Dieu kien do: pin cap qua chan B+, **da rut USB**, dong ho DT-9205A thang
200mA DC (do phan giai 0.1mA), do noi tiep tren duong B+.

> ⚠️ **LUON RUT USB TRUOC KHI CAM DONG HO VAO DUONG PIN.** Do dong luc dang
> sac da lam **chet IC sac** cua board XIAO — xem `../Beacon_XiaoNrf52840/README.md`.

### Qua trinh loai tru (moi dong = 1 lan build + flash + do lai)

| # | Cau hinh | Do duoc | Ket luan |
|---|---|---|---|
| 1 | Ban dau (co USB + log) | 1.9 mA | Cao gap ~100x ly thuyet |
| 2 | Tang buffer USB (`UDC_BUF_*`) | 1.9 mA | Het loi `net_buf` nhung dong khong doi |
| 3 | `CONFIG_LOG=n` | 1.9 mA | Tat log KHONG an — no chi chan viec *in ra* |
| 4 | Tat uart0/i2c0/i2c1/spi2 | 1.8 mA | Chan tha noi khong phai thu pham |
| 5 | Tat chan VCC ngoai (P0.13 LOW) | 1.7 mA | Co tac dung, nhung chi 0.1mA |
| 6 | Firmware **TRONG** (khong BLE/ADC) | 1.0 mA | Con lai la phan cung + USB |
| 7 | Firmware trong + P0.13 LOW | 0.9 mA | **San** |
| 8 | **Ban that: go han USB, giu BLE+ADC** | **0.9 mA** | ⬅️ **Ket qua cuoi** |

**Giam duoc 1.9 -> 0.9 mA (53%).**

### Doc ket qua

Dong #7 va #8 **bang nhau** — day la bang chung chinh: firmware day du (BLE
quang ba 1s + doc pin) tieu thu **duoi nguong do duoc** cua dong ho (0.1mA),
dung nhu ly thuyet Nordic Online Power Profiler (~12 µA @ interval 1000ms,
TX -4dBm). Tuc **phan firmware da toi uu xong, khong con gi de cat**.

0.9 mA con lai la **ro ri phan cung cua board clone** — firmware khong dong
toi duoc (chung minh bang dong #7: firmware TRONG cung van 0.9mA).

### Bai hoc quan trong nhat: USB khong tat bang cach tat log

Khoan tiet kiem lon nhat (**~0.8mA, gan mot nua tong dong**) la go tang USB.
Nhung phai go **dung cho**:

- `CONFIG_LOG=n` -> **vo dung**, chi chan in ra, tang USB van chay.
- `CONFIG_CONSOLE=n` + `SERIAL=n` -> **van chua du**: `.config` con
  `USB_DEVICE_STACK_NEXT=y` + `UDC_NRF=y`, va `CDC_ACM_SERIAL_ENABLE_AT_BOOT`
  mac dinh = y nghia la **USB duoc enable ngay luc boot** du app khong he goi
  `usb_enable()`.
- ✅ Dung: **`CONFIG_BOARD_SERIAL_BACKEND_CDC_ACM=n`** — cong tac chinh thuc,
  ca cum tu rung theo. Xem `prj.conf`.

### Tuoi tho pin uoc tinh (o 0.9 mA)

| Pin | Dung luong | Thoi gian |
|---|---|---|
| LiPo 1000mAh | 1000 mAh | ~46 ngay |
| LiPo 500mAh | 500 mAh | ~23 ngay |
| LIR2032 | ~40 mAh | ~2 ngay |

Neu sua duoc phan cung (ve ~15-20 µA) thi LiPo 500mAh se chay duoc **~3 nam**
(thuc te bi gioi han boi tu xa cua pin chu khong phai mach).

### Nguyen nhan 0.9 mA & tai sao KHONG sua

Theo repo reverse-engineer board nay
([sasodoma/nrf52840-promicro](https://github.com/sasodoma/nrf52840-promicro)):
cum power-path **NPQ2 (MOSFET) + NBD1 (diode) + NPR7**, va diode **W5** dang le
phai la Schottky BAT60B nhung nhieu ban clone lap nham diode silicon thuong ->
ro ri nguoc cao. Cach sua: thao 3 linh kien + cau chan 2-3 cua NPQ2.

**Quyet dinh: KHONG sua.** Ly do: hang SMD rat nho, rui ro hong board cao (da
mat 1 board XIAO vi su co phan cung), trong khi 0.9mA da du chay ~23 ngay voi
LiPo 500mAh — thua cho demo va lay so lieu thuc nghiem.

### Ghi chu ve pin

- ⚠️ **KHONG cam pin CR (CR2032/CR2477...) vao chan B+.** "CR" la pin lithium
  **so cap, KHONG sac lai duoc**; chan B+ co mach sac, cam USB vao la no bom
  dong sac -> phong/xi/no. Ban sac lai duoc la **LIR**.
- Coin cell **noi tro rat cao** (vai chuc Ω) -> xung TX cua BLE lam sut ap tuc
  thoi -> ADC doc nham -> **% pin nhay lung tung**. Da gap thuc te voi LIR2032:
  tut 100% -> 53% trong ~10 phut roi cam sac lai len 100% ngay (khong the sac
  day that trong vai giay). **Dung LiPo**, noi tro ~0.1-0.3Ω, khong bi hien
  tuong nay.

### Con lai (khong uu tien)

- [ ] Do lai o thang **2mA** (do phan giai 1µA thay vi 0.1mA) de co con so
      chinh xac hon cho bao cao — 0.9mA nam gon trong thang nay.
- [ ] Kiem tra `bmt_battery.c` doc dung dien ap pin that (so voi VOM).
- [ ] ~~Thu bat DC/DC qua devicetree~~ — **bo qua**: o 0.9mA thi MCU khong con
      la thanh phan chinh, bat DC/DC khong giai quyet duoc gi; hon nua so lieu
      ZMK cho thay tren board clone no con LAM TANG dong (xem `prj.conf`).
