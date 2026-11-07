# Benchmark Yöntemi — FNM-Bench 1.0 ve FNM-AI 1.0

Bu belge, FurUltimate benchmark skorunun nasıl üretildiğini eksiksiz tanımlar. Skor formülü ve sabitleri kaynak kodda `src/bench/Score.h` dosyasındadır ve bu belgeyle birebir aynıdır.

## 1. Genel ilkeler

- Her test sabit bir iş yükünü, sabit bir çözünürlükte ve sabit bir süre boyunca çalıştırır.
- Kamera yolları yalnızca sahne zamanının fonksiyonudur (deterministik); aynı ayarlar her çalıştırmada aynı görüntü dizisini üretir.
- Isınma (warm-up) süresi ölçüme dahil edilmez.
- İşleme çözünürlüğü ön ayara bağlıdır, pencere boyutundan bağımsızdır (çıktı pencereye oranı korunarak ölçeklenir).
- Erken durdurulan veya başarısız olan çalıştırmalar **genel skor almaz** (skor = 0, durum "aborted"/"failed").
- Her sonuç, benchmark sürümünü (`FNM-Bench 1.0`) ve bir **karşılaştırma anahtarını** saklar:
  `FNM-Bench 1.0|<ön ayar>|<FULL|QUICK>`. Uygulama yalnızca anahtarları eşleşen sonuçları karşılaştırır.

## 2. Ön ayarlar

| Ön ayar | İşleme çözünürlüğü |
|---|---|
| STANDARD-1080P | 1920 × 1080 |
| HIGH-1440P | 2560 × 1440 |
| ULTRA-2160P | 3840 × 2160 |

| Süre (sn) | Grafik | Shader | Hesaplama | VRAM | Kararlılık |
|---|---|---|---|---|---|
| FULL | 3 ısınma + 30 | 2 + 20 | 8 | 8 | 2 + 30 |
| QUICK | 2 + 12 | 1,5 + 8 | 4 | 4 | 1,5 + 12 |

`VALIDATION-270P` yalnızca QA/hat doğrulaması içindir (480×270, çok kısa süreler); karşılaştırılamaz.

## 3. Testler

### 3.1 Grafik İşleme (graphics)
"Nova Core" sahnesi: 160 adımlı ışın yürütme (SDF gyroid titanyum kabuk, emisif çekirdek, yörünge halkaları, kaide, yansıtıcı zemin), 2 yansıma sekmesi, 40 adımlı yumuşak gölgeler, 24 örnekli hacimsel ışık, 4 örnekli ortam kapatma, 3 oktav 3B doku tabanlı yüzey detayı; 40 000 prosedürel kristal parça (her biri 24 üçgen, köşe shader'ında üretilir); 262 144 GPU simülasyonlu parçacık; 128 MB VRAM akışı; bloom + ACES tonlama. Beş çekimden oluşan sinematik kamera dizisi.
**Ölçüm:** ortalama FPS = ölçülen kare sayısı / ölçülen duvar saati süresi.

### 3.2 Shader Performansı (shader)
Tam ekran Mandelbulb (üs 8) mesafe tahmini: 160 ışın adımı, 10 fraktal iterasyonu, 6 ek DE çağrısıyla normal hesabı. ALU ağırlıklıdır.
**Ölçüm:** ortalama FPS.

### 3.3 Hesaplama Performansı (compute)
1 048 576 iş parçacığı; her iş parçacığı 4 bağımsız `float4` zinciri üzerinde iterasyon başına 16 kez `x = x·m + c` uygular. Kalibrasyon, tek dispatch ≈ 12–25 ms olacak şekilde iterasyon sayısını ikiye katlayarak seçer (sürücü zaman aşımının çok altında).
FLOP sayısı = iş parçacığı × iterasyon × 16 × 4 zincir × 4 bileşen × 2 (çarpma + toplama).
Süre GPU zaman damgası sorgularıyla ölçülür. **Ölçüm:** örneklerin medyan değeri, **TFLOPS** (10¹² işlem/sn) olarak raporlanır. 1.1.1 ve önceki sürümlerin kayıtları GFLOPS olarak saklanmıştır; uygulama bunları otomatik olarak TFLOPS'a çevirerek gösterir ve karşılaştırır.

### 3.4 VRAM Performansı (vram)
İki adet 4096 genişliğinde `R32G32B32A32_UINT` yüzey kullanılır; her biri VRAM/8 boyutunda, 64–512 MB arasında.
1. **Doldurma:** Her iki yüzey, ölçümden önce sıkıştırılamayan sözde rastgele veriyle doldurulur. Sıfır veya tekrarlı veri kopyalansaydı GPU'nun bellek sıkıştırması gerçek dışı yüksek ve çalıştırmadan çalıştırmaya değişen sonuçlar üretirdi.
2. **Isınma:** FULL modda 1 sn, QUICK modda 0,6 sn boyunca kesintisiz kopyalama yapılır; bellek saatleri tam performans durumuna çıkar. Bu sürede parti boyutu, her parti yaklaşık 25 ms sürecek şekilde ayarlanır (2–64 geçiş).
3. **Ölçüm:** Taşınan bayt = 2 × yüzey boyutu × geçiş sayısı (okuma + yazma). Her partiden önce GPU kuyruğu boşaltılır; test penceresi en fazla saniyede 10 kez güncellenir, böylece ölçülen partilerin arasına başka iş girmez.

**Ölçüm:** medyan GB/s (10⁹ bayt/sn). Günlüğe ayrıca p10/p90 değerleri ve yayılım yazılır.

### 3.5 Kare Kararlılığı (stability)
Grafik sahnesinin daha hafif bir varyantı (128 adım, 16 hacimsel örnek, 20 000 parça) yörünge kamerasıyla.
**Ölçüm:** oran = %1 düşük FPS / ortalama FPS (yüzde olarak raporlanır).

## 4. Kare istatistikleri tanımları

- Kare süresi: ardışık `Present` çağrıları arasındaki CPU zamanı (QueryPerformanceCounter). Bu, PresentMon'un "MsBetweenPresents" tanımına karşılık gelir. Dikey senkronizasyon kapalıdır; destekleniyorsa tearing etkin flip-model swap chain kullanılır.
- Ortalama FPS = kare sayısı / toplam ölçüm süresi.
- Minimum / Maksimum FPS = 1 saniyelik pencerelerin ortalama FPS değerlerinin en küçüğü / en büyüğü (tek bir takılma minimumu yapay olarak sıfıra çekmez).
- %1 düşük FPS = 1000 / (kare sürelerinin 99. yüzdelik değeri, ms).
- Takılma (stutter) = medyan kare süresinin 2,5 katından ve 8 ms'den uzun kare.

## 5. Skor formülü

Alt skor = 1000 × ölçüm / referans değer (en az 1).

| Test | Referans |
|---|---|
| Grafik İşleme | 60 FPS |
| Shader Performansı | 60 FPS |
| Hesaplama Performansı | 10 TFLOPS (10 000 GFLOPS) |
| VRAM Performansı | 400 GB/s |
| Kare Kararlılığı | 1000 × min(1, %1 düşük / ortalama) |

Genel skor, ağırlıklı geometrik ortalamadır:

```
Skor = exp( 0,35·ln G + 0,20·ln S + 0,20·ln C + 0,15·ln V + 0,10·ln F )
```

Referans değerler belirli bir donanıma bağlı değildir; ölçeği tanımlayan sabitlerdir. Tüm alt skorlar 1000 ise genel skor 1000'dir. Geometrik ortalama, tek bir testteki aşırı değerin genel skoru orantısız şekilde domine etmesini önler.

## 6. Ölçümün geçerliliği ve kısıtlar

- Telemetri (sıcaklık, yük, güç, saat) raporlara eklenir ancak skora **dahil edilmez**.
- Sıcaklık acil durdurma eşiği aşılırsa çalıştırma durdurulur ve skor verilmez.
- Tek bir karenin GPU süresi 1,5 sn'yi aşarsa (Windows TDR sınırı 2 sn), benchmark donanımı korumak için durdurulur ve daha düşük bir ön ayar önerilir.
- Arka plan uygulamaları, güç planı, termal durum ve sürücü sürümü sonuçları etkiler. Karşılaştırmalarda bu koşulları sabit tutun.
- Bir skor, GPU'nun sağlıklı veya arızalı olduğunu göstermez.

## 7. Yapay Zekâ Performansı (6. test, FNM-AI 1.0)

AI testi **genel skora dahil değildir**; ayrı bir **AI skoru** üretir. Bu nedenle genel skor formülü 1.1 sürümünde değişmemiştir ve `FNM-Bench 1.0` sonuçları karşılaştırılabilir kalır.

**İş yükü:** sinir ağlarının temel işlemi olan matris çarpımı (C = A × B). İki yol sırayla ölçülür:

| Yol | Uygulama | Hassasiyet | Süre (FULL / QUICK) |
|---|---|---|---|
| Shader | Direct3D 11 hesaplama shader'ı, 2048 × 2048 matris, 64 × 64 karo, paylaşımlı bellek (desteklemeyen derleyicilerde önbellek tabanlı eşdeğer çekirdek) | FP32 | süre × 0,4 |
| DirectML | Windows'un makine öğrenmesi motoru, GEMM operatörü, 4096 × 4096 matris; FP16 destekleniyorsa yarı hassasiyet etkin (NVIDIA'da Tensor Core'lar) | FP16 (yoksa FP32) | süre × 0,6 |

AI testi FULL modda 12 sn, QUICK modda 6 sn sürer.

- İşlem sayısı = 2 × n³ (her çarp-topla 2 işlem). TOPS = işlem / saniye / 10¹².
- Ölçüm GPU zaman damgalarıyla yapılır (Direct3D 11 veya 12); örneklerin medyanı alınır.
- Sonuç olarak iki yoldan **yüksek olanı** raporlanır ve yolu kaydedilir (`DirectML FP16`, `DirectML FP32` veya `Shader FP32`). Farklı yollar birbiriyle karşılaştırılmaz; AI karşılaştırma anahtarı `FNM-AI 1.0|<yol>` biçimindedir.
- **AI skoru = 1000 × ölçülen TOPS / 50** (en az 1).
- **Teorik TOPS:** üreticilerin açıkladığı yoğun (seyreklik kullanılmayan) FP16 tensör/matris tepe değerleridir, FP16 biriktirme ile. Bilinen NVIDIA RTX 20/30/40 ve AMD RX 7000 kartları için tablo olarak saklanır (`src/bench/AiBench.cpp`). Tabloda olmayan kartlarda **N/A** gösterilir; değer tahmin edilmez. Teorik değer yalnızca bilgi amaçlıdır ve skora girmez.

## 8. Tek test çalıştırmaları

Her test Benchmark sayfasından tek başına çalıştırılabilir. Tek (veya kısmi) çalıştırmalar:
- Yalnızca ilgili testin alt skorunu üretir. **Genel skor verilmez**, çünkü genel skor beş testin tamamını gerektirir.
- Kendi karşılaştırma anahtarını alır (ör. `FNM-Bench 1.0|STANDARD-1080P|FULL|T:shader`); yalnızca aynı testin aynı ayarlı sonuçlarıyla karşılaştırılır.
- AI testi tek başına çalıştırıldığında AI skoru yine hesaplanır.

## 9. Zaman ölçümünün doğrulanması

Hesaplama, VRAM ve AI testlerinde her ölçüm partisinden önce GPU kuyruğu boşaltılır; ardından parti hem GPU zaman damgalarıyla hem de duvar saatiyle ölçülür. Zaman damgası sorguları ancak iş bittikten sonra sonuçlandığı için GPU süresi duvar saati süresinden belirgin biçimde kısa olamaz. GPU süresi duvar saatinin yarısından kısa çıkarsa (ve parti 2 ms'den uzunsa) zaman damgaları hatalı kabul edilir ve **duvar saati kullanılır**; bu durum günlüğe yazılır. Bu koruma, hatalı sürücülerin gerçek dışı yüksek sonuçlar üretmesini engeller. Geliştirme sırasında yazılımsal bir sürücüde tam olarak bu hata gözlenmiş ve düzeltilmiştir.

## 10. Liderlik tablosu kuralları

- Genel panolara yalnızca **tam (FULL) benchmark** sonuçları girer: tek test, hızlı mod ve doğrulama modu girmez.
- AI panoları yola göre ayrılır (`ai:DirectML FP16` vb.).
- Yalnızca kullanıcının yerel geçmişindeki **kişisel rekoru** (aynı pano, aynı ekran kartı) geçen skorlar gönderilir. Sunucu da yalnızca daha yüksek skorlarla günceller.
- Sunucu, genel skoru gönderilen alt skorlardan bu belgedeki formülle yeniden hesaplar ve tutarsız gönderimleri reddeder.

## 11. Sürümleme

İş yükü, süre, referans veya ağırlıklarda yapılacak her değişiklik yeni bir benchmark sürümü gerektirir (`FNM-Bench 1.1`, `2.0` …). Farklı sürümlerin sonuçları birbiriyle karşılaştırılmaz.
