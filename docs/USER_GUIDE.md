# Kullanıcı Kılavuzu

## Kurulum
1. `FurUltimate-1.0.0-Setup.exe` dosyasını çalıştırın. Kurulum yönetici izni ister (Program Files'a kurulur).
2. İsteğe bağlı olarak masaüstü kısayolu oluşturun.
3. Taşınabilir sürüm için ZIP arşivini açıp `FurUltimate.exe` dosyasını doğrudan çalıştırabilirsiniz.

Gereksinimler: Windows 10 (1809+) veya Windows 11, 64-bit; Direct3D 11 özellik seviyesi 11_0 destekleyen GPU; güncel GPU sürücüsü.

Uygulama verileri `%LOCALAPPDATA%\FurUltimate` altında tutulur (önceki AETHER GPU LAB sürümünün verileri ilk açılışta otomatik taşınır): `results.db` (sonuçlar), `settings.json`, `logs\`, `screenshots\`, `shadercache\`.

## Gezinme
Sol kenar çubuğu altı sayfaya erişim sağlar: GPU Stres Testi, Benchmark, Liderlik Tablosu, Sonuçlar, Donanım, Ayarlar. Kısayollar: **Ctrl+1 … Ctrl+6** sayfalar, **Tab / Shift+Tab** odak, **Enter / Boşluk** etkinleştirme, **← →** segment ve kaydırıcı, **F12** pencere ekran görüntüsü. Bir test çalışırken sağ üstte durum rozeti görünür; tıklayınca ilgili sayfaya gider.

## GPU Stres Testi
1. Ön ayar seçin:
   - **ECO** — düşük yük, ekran yenileme hızına kilitli.
   - **BALANCED** — orta sürekli yük (önerilen).
   - **EXTREME** — yoğun shader, geometri, hesaplama ve bellek yükü.
   - **CUSTOM** — 11 parametre ve 5 iş yükü türünü (fragment, geometri, doku, hesaplama, bellek) ayrı ayrı ayarlayın.
2. Çözünürlük, işleme kalitesi (0,5×–1,5×), süre (1/5/10/30/60 dk veya Sürekli) ve görüntü modunu seçin.
3. **Stres testini başlat**. Onay penceresi güvenlik eşiklerini gösterir.
4. Test penceresinin sol üstünde tam **Durum paneli** görünür: FPS ve süre halkası, sıcaklık, GPU yükü, kare ve GPU süresi, güç, ortalama/minimum FPS, sıcaklık eşikleri çubuğu ve uyarılar. Panel seçili temanın renklerini kullanır; Ayarlar → "Test penceresinde FPS katmanı" ile kapatılabilir.
5. Test penceresinde: **Esc** anında durdurur, **Boşluk** duraklatır/devam ettirir, **F12** veya **S** ekran görüntüsü alır.

Güvenlik: GPU sıcaklığı uyarı eşiğini aşınca uyarı gösterilir; acil durdurma eşiğini (veya sıcak nokta için eşik + 10 °C) aşınca test durdurulur. Bir kare 400 ms'yi aşarsa yük otomatik azaltılır (Ayarlar → GPU zaman aşımı koruması). Sürücü sıfırlanırsa test güvenli şekilde durur ve neden kaydedilir. Sıcaklık sensörü okunamıyorsa açık bir uyarı gösterilir — testi gözetim altında çalıştırın.

Test bittiğinde (tamamlanmış veya durdurulmuş) sonuç Sonuçlar sayfasına kaydedilir.

## Benchmark
Ön ayar (1080p / 1440p / 2160p), Hızlı mod ve Tam ekran seçin.
- **Tüm testleri başlat**: altı testi sırayla çalıştırır. Bunlar beş skorlu test (Grafik, Shader, Hesaplama, VRAM, Kare Kararlılığı) ve Yapay Zekâ Performansı. Sonuçta FurUltimate genel skoru ve ayrı **AI skoru** gösterilir.
- **Tek test**: her test satırındaki **▶** butonu yalnızca o testi çalıştırır. Tek test çalıştırmaları alt skor üretir; genel skor yalnızca beş skorlu testin tamamıyla hesaplanır. Bu kayıtlarda genel skor yerine "—" görünür.
- **Yapay Zekâ Performansı**: matris çarpımı hızını ölçer ve sonucu **AI skoru (ölçülen TOPS · teorik TOPS)** biçiminde gösterir. Örnek: `1706 (85.3 TOPS · teorik 119 TOPS)`. Teorik değer, bilinen kartlar için üreticinin açıkladığı FP16 tepe değeridir; bilinmeyen kartlarda gösterilmez.

Ayrıntılar: [Benchmark yöntemi](BENCHMARK_METHODOLOGY.md).

## Liderlik Tablosu
Dünya genelindeki sonuçları gösterir. Bunun için dağıtımda bir liderlik sunucusu yapılandırılmış olmalıdır; yapılandırılmamışsa sayfa bunu belirtir.
- Pano seçimi: **1080p / 1440p / 2160p** (tam benchmark genel skoru) veya **Yapay Zekâ** (AI skoru).
- **Oyuncular** görünümü en yüksek skorları, **Ekran kartları** görünümü her GPU modelinin ortalamasını gösterir. Kendi satırınız ve kartınız vurgulanır.
- Sağdaki **Benim kartım** bölümü kartınızın ortalamasını, kartlar arasındaki sırasını, yüzdelik dilimini ve kişisel rekorunuzu gösterir.
- **Katılım isteğe bağlıdır.** İlk uygun benchmark sonucunda bir kez sorulur; takma adınızı girip katılırsınız. Ayarlar → Liderlik tablosu bölümünden istediğiniz zaman açıp kapatabilir, takma adınızı değiştirebilirsiniz.
- **Yalnızca kişisel rekorunuzu geçen skorlar gönderilir.** Rekor; ön ayar ve ekran kartı başına ayrı tutulur. Yeni rekorda "Yeni kişisel rekor!" bildirimi çıkar.
- Gönderilen veriler: takma ad, ekran kartı modeli, sürücü sürümü, skorlar. Kişisel bilgi gönderilmez. Kurulumunuz, kayıtlarınızı tanımak için rastgele ve anonim bir oyuncu kimliği kullanır.

## Sonuçlar
Sol listeden kayıt seçin; Tümü/Benchmark/Stres filtreleri. İşlemler: **PDF**, **JSON**, **CSV** dışa aktarma, **Karşılaştırmaya ekle** (ikinci bir sonuç seçtiğinizde farklar yüzde olarak gösterilir; sürüm/ayarlar farklıysa karşılaştırma reddedilir), ekran görüntüsünü açma, silme.

## Donanım
GPU, API desteği, CPU, sistem belleği, işletim sistemi ve ekranlar. Algılanamayan değerler N/A. **Panoya kopyala** destek talepleri için düz metin özet üretir.

## Ayarlar
Dil (Türkçe/English), başlangıç sayfası, büyütülmüş başlatma, başlangıçta izleme; **7 renk teması** (Grafit, Gece, Okyanus, Zümrüt, Kehribar, Bordo, Açık; renk önizlemeli kutucuklardan seçilir ve anında uygulanır), hareketi azalt, grafik ızgarası; telemetri yenileme aralığı, °C/°F; liderlik tablosu katılımı ve takma ad; test penceresi FPS katmanı ve kare süresi; benchmark tam ekran varsayılanı; sıcaklık eşikleri, varsayılan stres süresi, onay ve TDR koruması; veri/ekran görüntüsü/günlük klasörleri, geçmişi silme, varsayılanlara dönme. Değişiklikler otomatik kaydedilir.

## Komut satırı (otomasyon / QA)
```
FurUltimate.exe --page=2 --autorun=bench-quick --exit-after-run
```
Seçenekler: `--page=N` (0 Stres … 5 Ayarlar), `--autorun=stress-eco|stress-balanced|stress-extreme|bench|bench-quick|bench-validation`, `--tests=graphics,shader,compute,vram,stability,ai` (benchmark test seçimi), `--duration=SANİYE` (stres), `--capture-after=SANİYE`, `--exit-after-run`, `--data-dir=YOL`. Otomasyonla başlatılan testler onay penceresi göstermez; güvenlik eşikleri yine uygulanır.
