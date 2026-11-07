# Donanım Test Prosedürü

Bu prosedür, geliştirme ortamında GPU bulunmadığı için gerçek donanımda doğrulanması gereken işlevleri kapsar. Her adımda beklenen sonuç yazılıdır; sapmaları günlük dosyasıyla (`%LOCALAPPDATA%\FurUltimate\logs`) birlikte raporlayın.

## Hazırlık
- Test makineleri: en az bir NVIDIA, bir AMD ve bir Intel (tümleşik veya Arc) GPU; Windows 10 22H2 ve Windows 11 24H2.
- Güncel sürücüler; dizüstü bilgisayarlarda prize takılı ve "En iyi performans" güç modu.
- Referans araç (isteğe bağlı): GPU-Z / HWiNFO ile sensör karşılaştırması.

## 1. Otomatik testler
```
aether_tests.exe
```
Beklenen: `16 passed, 0 failed`. (Shader derleme testi ilk çalıştırmada birkaç saniye sürebilir.)

## 2. Başlangıç ve algılama
1. Uygulamayı başlatın. Beklenen: 1 sn içinde pencere; 1–3 sn içinde GPU adı, VRAM, sürücü sürümü.
2. Donanım sayfası: Vendor/Device/Subsystem ID'leri Aygıt Yöneticisi → Ayrıntılar → Donanım Kimlikleri ile karşılaştırın. Sürücü sürümü/tarihi ve PCIe Gen/x genişliği GPU-Z ile eşleşmeli.
3. Sistem belleği türü/hızı SMBIOS'tan gelir; Görev Yöneticisi → Performans → Bellek ile karşılaştırın.

## 3. Telemetri doğruluğu
1. İzleme sayfası, 1 dk. Boşta 2 dakika bekleyin; GPU sıcaklığı, yük, saatler, güç, fan değerlerini HWiNFO ile karşılaştırın (±2 °C, ±%5 yük, ±%5 güç kabul).
2. Kartlardaki kaynak etiketlerini not edin (NVML/ADL/D3DKMT/PDH).
3. Desteklenmeyen sensörler N/A olmalı; hiçbir kart sıfır veya sabit bir uydurma değer göstermemeli.

## 4. Stres testi
1. BALANCED, 1080p, 1.0×, 5 dk, Pencere. Beklenen: onay penceresi; test penceresinde sahne, FPS katmanı; GPU yükü %95+ (vsync kapalı); UI akıcı kalır.
2. Boşluk ile duraklatın: yük düşmeli; tekrar Boşluk ile devam.
3. Esc ile durdurun: pencere < 1 sn'de kapanmalı; Sonuçlar'da "Durduruldu" kaydı.
4. ECO: FPS ekran yenileme hızında sabitlenmeli.
5. EXTREME, 2160p, 1.5× ile en zayıf GPU'da: kare süresi 400 ms'yi aşarsa "yük otomatik azaltıldı" uyarısı; sürücü sıfırlaması olmamalı.
6. Eşik testi: Ayarlar → Uyarı eşiğini (mevcut boş sıcaklık + 5 °C), acil durdurmayı (+10 °C) yapın, BALANCED başlatın. Beklenen: uyarı bandı, ardından "Acil durdurma: GPU sıcaklığı …" ile otomatik durdurma. Eşikleri geri alın.
7. Tam ekran modu: test birincil olmayan monitörde de (UI hangi monitördeyse) açılmalı; Alt+Tab sonrası Esc çalışmalı.
8. 60 dk BALANCED: bellek kullanımı (Görev Yöneticisi) sabit kalmalı (sızıntı yok).

## 5. Cihaz kaybı
1. Otomatik: `aether_tests.exe device_lost` → OK.
2. Manuel (isteğe bağlı, yönetici): stres testi çalışırken `dxcap -forcetdr` (Windows Graphics Tools) ile TDR tetikleyin. Beklenen: test "GPU sürücüsü sıfırlandı…" mesajıyla durur, uygulama çökmez, ardından yeni bir test başlatılabilir; UI yeniden çizilmeye devam eder.

## 6. Benchmark
1. Standart 1080p, FULL. Beklenen süre ≈ 1:45. Beş test sırayla; sonunda skor.
2. Aynı ayarlarla 3 kez çalıştırın; skorlar arasında ≤ %3 fark beklenir (termal durum sabitken).
3. Hızlı mod ile çalıştırın: karşılaştırma anahtarı QUICK olmalı; FULL sonuçla karşılaştırma "Karşılaştırılamaz" göstermeli.
4. Hesaplama (TFLOPS) değerini GPU'nun teorik FP32 tepe değerinin (RTX 3080: 29,8 TFLOPS) %60–95'i aralığında beklenir; VRAM GB/s teorik bant genişliğinin %60–90'ı.

## 7. Raporlar
1. Sonuçlar → PDF / JSON / CSV. PDF'i Edge ve Adobe Reader'da açın: Türkçe karakterler, metin seçimi/kopyalama, grafik ve tablo doğru.
2. Bir sonucu karşılaştırmaya ekleyip diğerinin PDF'ini alın: karşılaştırma bölümü yüzde farklarla.
3. F12 ile ekran görüntüsü: `screenshots` klasöründe PNG.

## 8. Yüksek DPI ve pencere boyutları
1920×1080 %100, 2560×1440 %125, 3840×2160 %150/%200; pencereyi monitörler arası taşıyın, en küçük boyuta küçültün, büyütün. Beklenen: keskin metin, taşma yok, kaydırma çalışır.

## 9. Kurulum
Temiz bir Windows'ta kurulum → başlat → kaldır. Beklenen: Program Files, Başlat menüsü, (seçildiyse) masaüstü kısayolu; "Uygulamalar ve özellikler" listesinde simge, sürüm ve yayıncı; kaldırma sonrası dosya kalmaz (kullanıcı verileri isteğe bağlı silinir).

## 10. Yapay Zekâ Performansı (1.1.0)
1. Benchmark → Yapay Zekâ Performansı satırındaki ▶ ile yalnızca AI testini çalıştırın.
2. Günlükte `AI DirectML GEMM 4096 FP16: … TFLOPS` satırını arayın. "DirectML path unavailable" yazıyorsa nedenini not edin.
3. Beklenen: RTX 3080 için DirectML FP16 değeri teorik 119 TOPS'un yaklaşık %40–80'i; shader FP32 değeri ise FP32 tepe değerinin (~29,8 TFLOPS) %30–70'i.
4. Sonuç ekranında "AI skoru X (Y TOPS · teorik Z TOPS)" biçimini ve yol etiketini (DirectML FP16) kontrol edin.
5. Günlükte "timestamps inconsistent" uyarısı **olmamalıdır** (gerçek sürücüde zaman damgaları güvenilirdir).

## 11. Tek test ve 1 dakikalık stres
1. Her benchmark testini ▶ ile tek tek çalıştırın. Sonuç kartında "Tek test çalıştırması" yazmalı ve genel skor "—" olmalıdır.
2. Stres testini 1 dk süreyle başlatın. Test 60 sn sonra kendiliğinden bitmeli; test penceresindeki Durum paneli tüm metrikleri göstermelidir.

## 12. Liderlik tablosu
1. `server/leaderboard/README.md` adımlarıyla bir Supabase projesi kurun ve `leaderboard.json` dosyasını exe'nin yanına koyun.
2. Tam bir benchmark (Standart 1080p, FULL) çalıştırın. Katılım penceresi çıkmalı; takma ad girip katılın. "Yeni kişisel rekor" bildirimi gelmeli.
3. Liderlik Tablosu sayfasında satırınız vurgulu görünmeli; "Benim kartım" bölümü dolmalı.
4. Aynı benchmark'ı tekrar çalıştırın. Skor düşükse gönderim olmamalı; yüksekse kayıt güncellenmeli.
5. İkinci bir bilgisayardan katılın; Ekran kartları görünümünde ortalamalar oluşmalı.
