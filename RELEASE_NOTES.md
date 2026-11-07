# Sürüm Notları

## 1.2.0 — 2026-11-07
- **Yeni ad: FurUltimate** (önceden FurNovaMark). Sonuçlar, ayarlar ve liderlik kimliği ilk açılışta yeni klasöre (`%LOCALAPPDATA%\FurUltimate`) otomatik taşınır.
- **Yeni logo:** degrade altıgen rozet, canlı alev çekirdeği, dönen enerji halkaları. Kenar çubuğunda animasyonlu; uygulama simgesi, kurulum görselleri ve PDF raporlarında da yeni logo kullanılıyor.
- **Hızlı mod artık liderlik tablosuna girmiyor:** hızlı modla alınan genel ve AI skorları gönderilmez (önceden AI skoru gönderilebiliyordu). Tabloya girmek için hızlı modu kapatıp tam benchmark çalıştırın.
- **Tek dosya exe:** liderlik sunucusu adresi derleme sırasında exe'ye gömülüyor. Kurulum paketi, tek exe ve ZIP ek dosya olmadan çevrimiçi tabloya bağlanıyor.
- Kaynak kod MIT lisansıyla açık kaynak olarak yayımlandı.

## 1.1.4 — 2026-11-07
- **Çevrimiçi liderlik tablosu etkin:** paket, FurNovaMark Supabase sunucusuna bağlanacak şekilde yapılandırıldı (`leaderboard.json`). Sunucu şeması kuruldu ve doğrulandı.
- **AI testi:** DirectML aşamasında ekranda 0 yerine canlı TOPS değeri gösteriliyor. Alt süreç ara sonucunu ana pencereye iletiyor. Ölçüm sonucu daha önce de doğruydu; RTX 3080'de 82,6 TOPS ölçüldü.
- Isınma ve kalibrasyon ölçümlerinde çıkan yanıltıcı "zaman damgası tutarsız" uyarısı kaldırıldı. Bu ölçümlerde sürücünün ilk kullanım hazırlığı beklenen bir durum.

## 1.1.3 — 2026-11-07
**Düzeltme: VRAM testi her çalıştırmada farklı sonuç veriyordu.** Nedenleri ve çözümler:
- **Kaynak bellek doldurulmuyordu:** test sıfırları kopyalıyordu. GPU'nun bellek sıkıştırması bu yüzden bant genişliği yerine sıkıştırma verimini ölçüyordu. Artık her iki yüzey sıkıştırılamayan rastgele veriyle dolduruluyor.
- **Ölçümlerin arasına ekran çizimi giriyordu:** her kısa partiden önce test penceresine bir kare çiziliyordu ve bu kare GPU kuyruğunda partinin önüne giriyordu. 1.1.0'daki zaman damgası koruması bu bekleme süresi nedeniyle bazı örneklerde duvar saatine geçiyor, sonuçlar iki farklı değer arasında gidip geliyordu. Artık her partiden önce GPU kuyruğu boşaltılıyor ve pencere en fazla saniyede 10 kez güncelleniyor.
- **Partiler çok kısaydı (~5 ms):** şimdi ~25 ms. **Isınma da çok kısaydı:** şimdi 1 sn; bellek saatleri ölçümden önce en yüksek seviyeye çıkıyor.
- Aynı kuyruk boşaltma ve seyreltilmiş ekran güncellemesi Hesaplama (Test 3) ve AI testine de uygulandı; Test 3'e 1 sn ısınma eklendi.
- Günlüğe VRAM ölçümünün dağılımı (p10/p90, yayılım %) yazılıyor.

**Not:** Bu düzeltme VRAM ve Hesaplama sonuçlarını değiştirir. 1.1.3 ile alınan sonuçları önceki sürümlerle karşılaştırmayın.

## 1.1.2 — 2026-11-07
- **Hesaplama Performansı (Test 3) artık TFLOPS gösteriyor** (önceden GFLOPS). Örnek: 24,15 TFLOPS. Değişiklik canlı ekranda, test listesinde, sonuçlarda, karşılaştırmalarda, PDF ve CSV'de geçerli. Skor formülü değişmedi; eski kayıtlar otomatik olarak TFLOPS'a çevrilerek gösteriliyor ve doğru karşılaştırılıyor.
- **Yapay Zekâ Performansı (Test 6)** sonucu TOPS olarak gösterilmeye devam ediyor. Gösterim ortak biçimlendiriciye bağlandı.

## 1.1.1 — 2026-11-07
**Düzeltme: Benchmark, Yapay Zekâ testine geldiğinde uygulama çöküyordu.**
- Neden: DirectML'in `GetBindingProperties()` metodu bir yapıyı (struct) değer olarak döndürür. MinGW/GCC bu tür COM metotlarını Microsoft'tan farklı bir çağırma düzeniyle derler. MinGW kendi D3D12/D2D başlıkları için bunu düzeltir, ama Microsoft'un DirectML.h başlığında böyle bir düzeltme yoktur. Sonuçta DirectML yolu çalıştığı anda yığın bozuluyor ve uygulama çöküyordu. Geliştirme ortamında DirectML bulunmadığı için bu yol daha önce hiç çalışmamıştı.
- Çözüm: metot, sanal tablo üzerinden Microsoft çağırma düzeniyle çağrılıyor. Bu, birim testle doğrulandı.
- Ek güvenlik: DirectML ölçümü artık **ayrı bir alt süreçte** çalışıyor. DirectML veya sürücü içinde başka bir çökme olsa bile uygulama kapanmaz; hata günlüğe yazılır ve AI testi shader sonucuyla tamamlanır. Çöken alt süreç hata penceresi açmadan anında sonlanır.
- 2 yeni test (24 test): ABI doğrulaması ve hata enjeksiyonlu alt süreç çökme testi.

## 1.1.0 — 2026-11-06
**Yeni ad:** uygulama artık **FurNovaMark**. Önceki sürümün verileri (sonuçlar, ayarlar, ekran görüntüleri) ilk açılışta otomatik taşınır.

**Yeni**
- **Yapay Zekâ Performansı** (Benchmark'ın 6. testi): Direct3D 11 karo matris çarpımı (FP32) ve DirectML (FP16, Tensor Core). Sonuç ayrı bir **AI skoru** olarak verilir; ölçülen TOPS ve bilinen kartlar için teorik TOPS parantez içinde gösterilir. Genel skor formülü değişmedi.
- **Testleri tek tek çalıştırma:** her benchmark testinin yanında ▶ butonu. Tek test sonuçları alt skor üretir, genel skor üretmez.
- **Çevrimiçi Liderlik Tablosu** (sol menü): 1080p / 1440p / 2160p / Yapay Zekâ panoları, oyuncu ve ekran kartı sıralamaları, "Benim kartım" özeti. İsteğe bağlı katılım; yalnızca kişisel rekoru geçen skorlar gönderilir. Supabase sunucu şeması ve kurulum kılavuzu `server/leaderboard/` altında.
- **Test penceresinde tam Durum paneli:** FPS ve süre halkası, sıcaklık, GPU yükü, kare ve GPU süresi, güç, ortalama/minimum FPS, eşik çubuğu ve uyarılar.
- **Stres testinde 1 dakikalık süre** seçeneği.
- **7 renk teması:** Grafit, Gece, Okyanus, Zümrüt, Kehribar, Bordo ve Açık. Renk önizlemeli seçim; başlık çubuğu ve test penceresi paneli de temaya uyar.
- Metin kutusu bileşeni (takma ad girişi, Türkçe karakter desteği).

**Kaldırılan**
- Genel Bakış ve İzleme sayfaları (isteğe göre). Stres testi güvenliği için telemetri çalışmaya devam eder.

**Düzeltmeler**
- Hatalı GPU zaman damgası raporlayan sürücülerde hesaplama, VRAM ve AI sonuçlarının gerçek dışı yüksek çıkması engellendi; zaman damgaları duvar saatiyle doğrulanıyor.
- Test sayısı 16'dan 22'ye çıktı; gerçek bir PostgREST sunucusuna karşı liderlik entegrasyon testi eklendi.

## 1.0.1 — 2026-11-05
- **Düzeltme:** Fare tıklamaları hiçbir düğme, gezinme öğesi veya kontrol tarafından algılanmıyordu (bırakma olayından önce aktif öğe sıfırlanıyordu). Tüm arayüz artık fareyle kullanılabilir.
- Gerçek donanımda doğrulandı: NVIDIA RTX 3080 üzerinde NVML telemetrisi (sıcaklık, yük, güç, saatler, fan, PCIe) çalışıyor.

## 1.0.0 — 2026-11-05 (ilk sürüm)

**Uygulama**
- Direct2D/DirectWrite tabanlı yerel premium arayüz: grafit tasarım dili, Inter yazı tipi, özgün ikon seti ve logo, sayfa geçişleri, odak halkaları, ipuçları, bildirimler, Per-Monitor V2 yüksek DPI.
- Yedi sayfa: Genel Bakış, GPU Stres Testi, Benchmark, İzleme, Sonuçlar, Donanım, Ayarlar.
- Türkçe (birincil) ve İngilizce arayüz.

**GPU iş yükleri**
- Özgün "Nova Core" sahnesi: SDF ışın yürütme, gyroid titanyum kabuk, çok sekmeli yansımalar, yumuşak gölgeler, hacimsel ışık huzmeleri, 3B doku örnekleme, prosedürel kristal halka (köşe shader'ında üretilen geometri), GPU parçacık simülasyonu, VRAM akış çekirdeği, bloom ve ACES tonlama.
- Stres testi: ECO / BALANCED / EXTREME / CUSTOM, çözünürlük ve kalite ölçeği, süre sınırı veya sürekli mod, duraklat/devam/durdur.
- Benchmark FNM-Bench 1.0: Grafik, Shader (Mandelbulb), Hesaplama (FMA GFLOPS), VRAM (GB/s), Kare Kararlılığı; belgelenmiş skor formülü; FULL/QUICK.

**Güvenlik**
- Ayarlanabilir uyarı ve acil durdurma sıcaklık eşikleri, sıcak nokta koruması, GPU zaman aşımı (TDR) koruması, sınırlı VRAM ayırma, cihaz kaybında güvenli durdurma ve yeniden oluşturma, yoğun testlerden önce onay.

**Telemetri ve donanım**
- NVIDIA NVML, AMD ADL Overdrive8, Windows D3DKMT, PDH sağlayıcıları; kaynak gösterimi; eksik sensörler N/A.
- GPU/CPU/RAM/ekran/OS algılama, PCIe bağlantısı, sürücü tarihi, D3D12 özellik seviyesi ve Shader Model.

**Sonuçlar**
- SQLite geçmişi, karşılaştırma (yalnızca uyumlu sürüm/ayarlar), markalı PDF (gömülü font, seçilebilir metin), JSON, CSV, PNG ekran görüntüleri.

**Kalite**
- 16 otomatik birim/entegrasyon testi (hata enjeksiyonlu cihaz kaybı testi dahil).
- Statik bağlı tek exe; çalışma zamanı yeniden dağıtımı gerekmez.
- NSIS kurulum programı ve taşınabilir ZIP.
