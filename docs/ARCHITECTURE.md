# Mimari ve Geliştirme Süreci

## Faz 1 — Depo incelemesi
Çalışma dizini boştu (mevcut kod, derleme sistemi veya varlık yoktu). Geliştirme ortamı: Linux (Ubuntu 24.04) bulut konteyneri, GPU yok. Bu nedenle uygulama **MinGW-w64 (GCC 13, C++20) ile çapraz derlenir**; çalıştırma ve otomatik testler **Wine 9 + Mesa llvmpipe** (yazılımsal Direct3D 11) üzerinde yapılmıştır. Shader'lar ayrıca DXC ile sözdizimi açısından doğrulanır. Gerçek GPU sensörleri bu ortamda bulunmadığından, donanıma bağlı işlevler için [donanım test prosedürü](HARDWARE_TEST_PROCEDURE.md) hazırlanmıştır.

## Faz 2 — Mimari kararlar
- **Yerel UI çerçevesi:** Qt 6, MinGW çapraz derlemesinde büyük bir bağımlılık ve dağıtım yükü getirecekti. Bunun yerine Windows'un yerel 2B yığını (Direct2D 1.1 + DirectWrite) üzerine, uygulamaya özel immediate-mode bir bileşen katmanı yazıldı. Sonuç: tek bir 6 MB'lık statik exe, tam tasarım kontrolü, yüksek DPI (Per-Monitor V2), GPU hızlandırmalı çizim.
- **Direct3D 11** iş yükleri için seçildi (en geniş donanım/sürücü kapsamı, FL 11_0). D3D12 yalnızca yetenek sorgusunda kullanılır. DirectML bu sürümde iş yükü olarak kullanılmaz (bkz. Bilinen sınırlamalar).
- **Shader'lar** HLSL olarak kaynak ağacında tutulur, derleme zamanında exe'ye gömülür, çalışma zamanında `d3dcompiler_47.dll` ile derlenip diske önbelleğe alınır. Shader kodu, Wine'ın vkd3d derleyicisiyle de uyumlu olacak şekilde taşınabilir alt kümede yazılmıştır (tipli dokular, `SV_InstanceID` yerine vertex ID, özel atan2/acos).

## Modüller

| Modül | Dizin | Sorumluluk |
|---|---|---|
| Uygulama kabuğu | `src/main.cpp`, `src/ui/App.*` | Pencere, mesaj döngüsü, gezinme, modal pencereler, motor koordinasyonu, otomasyon |
| UI bileşenleri | `src/ui/Canvas.*, UI.*, Charts.*, Icons.*, Theme.*` | Direct2D tuval, tasarım belirteçleri, bileşenler, grafikler, ikon seti, logo |
| Sayfalar | `src/ui/pages/*` | Yedi sayfa |
| GPU cihaz yöneticisi | `src/gpu/GpuDevice.*` | DXGI bağdaştırıcı keşfi, sürücü sürümü, D3D11 cihazı, tearing desteği |
| Shader kütüphanesi | `src/gpu/ShaderLibrary.*`, `src/render/shaders/*.hlsl` | Gömülü HLSL, derleme, disk önbelleği |
| Render motoru | `src/render/SceneRenderer.*, RenderSurface.*, Math.h` | Hero sahne, iş yükleri, post-process, kameralar, GPU zamanlayıcı, test penceresi, swap chain, D2D katmanı |
| Stres motoru | `src/stress/*` | Ön ayarlar, iş parçacığı, güvenlik, TDR koruması, cihaz kaybı |
| Benchmark motoru | `src/bench/*` | 5 test, kare istatistikleri, skor modeli |
| Donanım algılama | `src/hw/*` | SetupAPI, D3D12 yetenekleri, CPUID, SMBIOS, ekranlar |
| Telemetri | `src/telemetry/*` | Örnekleme iş parçacığı, halka tampon, NVML/ADL/D3DKMT/PDH sağlayıcıları |
| Veritabanı | `src/data/*` | Sonuç modeli, JSON serileştirme, SQLite deposu |
| Raporlama | `src/report/*` | PDF yazıcı (TrueType gömme), PDF/JSON/CSV dışa aktarma |
| Yapılandırma | `src/core/Settings.*, Paths.*` | Ayarlar, sınırlandırma, atomik kayıt |
| Günlükleme | `src/core/Log.*` | İş parçacığı güvenli dosya günlüğü |

## İş parçacıkları
- **UI iş parçacığı:** pencere, çizim (vsync'e bağlı, boştayken uyur), veritabanı yazımları.
- **Telemetri iş parçacığı:** sağlayıcıları ayarlanan aralıkta örnekler; son örnek ve geçmiş mutex ile korunur.
- **İş yükü iş parçacığı:** stres veya benchmark; kendi penceresi, D3D11 cihazı ve mesaj döngüsü vardır. UI ile durum yapısı (mutex) ve atomik komutlar üzerinden konuşur; bitiş sonucu kuyruğa alınır ve UI iş parçacığı veritabanına yazar.
- **Donanım algılama iş parçacığı:** başlangıçta bir kez çalışır (D3D12 yoklaması zaman alabilir); UI bu sürede iskelet durumu gösterir.

Kapanışta: iş yükleri durdurulur ve beklenir, telemetri durdurulur, ayarlar kaydedilir, veritabanı kapatılır.

## Hata ve cihaz kaybı yönetimi
- İş yükü `Present` → `DXGI_ERROR_DEVICE_REMOVED/RESET/HUNG`: test güvenli şekilde durur, neden (`GetDeviceRemovedReason`) kaydedilir; sonraki test yeni bir cihaz ve swap chain oluşturur.
- UI `EndDraw` → `D2DERR_RECREATE_TARGET` veya Present cihaz kaybı: UI cihazı ve swap chain yeniden oluşturulur.
- Hata enjeksiyonu: `AETHER_SIMULATE_DEVICE_LOST=<n>` ortam değişkeni n. Present'te cihaz kaybı simüle eder (otomatik test kullanır).

## Faz özeti
| Faz | Durum |
|---|---|
| 1 İnceleme | Boş depo; çapraz derleme + Wine doğrulama ortamı kuruldu |
| 2 Mimari ve derleme sistemi | CMake, MinGW araç zinciri dosyası, MSVC uyumu, shader gömme |
| 3 Tasarım sistemi ve kabuk | Tema belirteçleri, Direct2D tuval, bileşenler, ikonlar, logo, geçişler |
| 4 Donanım algılama | DXGI, SetupAPI, D3D12, CPUID, SMBIOS, ekranlar |
| 5 GPU render | Hero sahne, post-process, test penceresi, GPU zamanlayıcı |
| 6 Stres motoru | Ön ayarlar, CUSTOM, güvenlik, TDR, cihaz kaybı |
| 7 Benchmark | 5 test, istatistikler, skor modeli, doğrulama modu |
| 8 İzleme | 4 sağlayıcı, halka tampon, grafikler |
| 9 Sonuçlar ve raporlama | SQLite, karşılaştırma, PDF/JSON/CSV, ekran görüntüsü |
| 10 Test ve yayın | 16 otomatik test, NSIS kurulum, belgeler |

## 1.1.0 eklemeleri

| Modül | Dizin | Sorumluluk |
|---|---|---|
| AI ölçümü | `src/bench/AiBench.*`, `src/bench/DmlCompat.h`, `src/render/shaders/aigemm*.hlsl` | Direct3D 11 karo GEMM (FP32) ve DirectML GEMM (FP16, Direct3D 12 üzerinde), teorik TOPS tablosu |
| Liderlik istemcisi | `src/net/Leaderboard.*` | WinHTTP ile REST çağrıları (arka plan iş parçacıkları), yapılandırma (`leaderboard.json`), gönderim ve sorgu anlık görüntüleri |
| Liderlik sunucusu | `server/leaderboard/schema.sql` | Supabase/PostgreSQL şeması, herkese açık görünümler, doğrulayan `submit_score()` ve `my_entries()` fonksiyonları |
| Liderlik sayfası | `src/ui/pages/LeaderboardPage.cpp` | Pano/görünüm seçimi, tablo, "Benim kartım", katılım |

- **Kaldırılanlar:** Genel Bakış ve İzleme sayfaları. Telemetri hizmeti kalır; stres testi güvenliği, Durum paneli ve kenar çubuğu onu kullanmaya devam eder.
- **Benchmark motoru:** test seçme maskesi (`BenchConfig::testMask`). İlerleme ve süre maskeye göre hesaplanır; kısmi çalıştırmalarda genel skor üretilmez.
- **Temalar:** `Theme` artık 7 palet içerir. Arayüzdeki saydam katmanlar `FG()` kontrast belirtecini kullanır, böylece açık tema da desteklenir. Test penceresi paneli temanın renklerini `OverlayPalette` ile alır.
- **Zaman ölçümü koruması:** GPU zaman damgaları duvar saatiyle karşılaştırılır (bkz. benchmark yöntemi §9).
- **Ad değişikliği:** ürün adı FurUltimate oldu. Veri klasörü `%LOCALAPPDATA%\FurUltimate` ve eski klasör ilk açılışta taşınır. Kod içi ad alanı (`aether::`) değişmedi.
