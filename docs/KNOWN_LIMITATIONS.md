# Bilinen Sınırlamalar

1. **Gerçek GPU üzerinde test edilmedi.** Geliştirme ortamında fiziksel GPU yoktu. Tüm render, stres, benchmark, raporlama ve kurtarma yolları Wine 9 + Mesa llvmpipe (yazılımsal Direct3D 11) üzerinde uçtan uca çalıştırıldı ve otomatik testlerle doğrulandı. Gerçek donanımda doğrulama için [donanım test prosedürünü](HARDWARE_TEST_PROCEDURE.md) uygulayın.
2. **Sensörler sürücüye bağlıdır.**
   - NVIDIA (NVML): sıcaklık, yük, bellek denetleyici yükü, çekirdek/bellek saati, güç (W), fan %, VRAM, PCIe. Sıcak nokta, VRAM sıcaklığı ve voltaj NVML'de herkese açık değildir → N/A.
   - AMD (ADL Overdrive8 PMLog): sıcaklık, sıcak nokta, bellek sıcaklığı, yük, saatler, ASIC gücü, fan RPM/%, voltaj, bellek türü. Sensör dizin eşlemesi ADL SDK tanımlarına göre uygulanmıştır ancak gerçek AMD donanımında doğrulanmamıştır.
   - Intel ve diğerleri: Windows D3DKMT performans verisi (Windows 10 2004+, WDDM 2.7+: sıcaklık, fan RPM, güç limiti %, saatler) ve PDH (yük, VRAM). Birçok tümleşik GPU sıcaklık bildirmez → N/A.
   - Sıcaklık okunamazsa acil durdurma koruması çalışamaz; uygulama bunu açıkça uyarır.
3. **Mimari adı** NVIDIA'da NVML'den gelir; diğer üreticilerde model adından çıkarılır ve `*` ile işaretlenir.
4. **Bellek türü** (GDDR6 vb.) yalnızca AMD ADL ile alınır; diğerlerinde N/A.
5. **DirectML** iş yükü olarak kullanılmaz; yalnızca sistem bileşeninin varlığı raporlanır. Vulkan/OpenCL için yalnızca yükleyici varlığı tespit edilir (cihaz doğrulanmaz).
6. **Kare süresi** CPU tarafında Present aralığı olarak ölçülür (PresentMon ile uyumlu tanım). Ekran kartı sürücüsünün kuyruk davranışı tek tek kare sürelerini etkileyebilir; ortalama FPS her zaman toplam kare / toplam süredir.
7. **Tam ekran modu** kenarlıksız tam ekran penceredir (exclusive fullscreen değil); flip-model ve tearing desteği varsa FPS sınırsızdır.
8. **Kod imzalama** yoktur. Windows SmartScreen ilk çalıştırmada uyarı gösterebilir. Dağıtımdan önce bir kod imzalama sertifikasıyla imzalanması önerilir.
9. **Dil:** Türkçe birincil dildir; İngilizce çeviri tablosu tamdır. Yeni dil eklemek için `tools/en_translations.py` örneğini izleyin.
10. **Shader önbelleği** ilk çalıştırmada derlenir; büyük sahne shader'ının derlenmesi yavaş sistemlerde birkaç saniye sürebilir ("Shader'lar hazırlanıyor…").

## 1.1.0 ile gelen sınırlamalar

11. **DirectML yolu gerçek donanımda henüz doğrulanmadı.** Geliştirme ortamında DirectML.dll bulunmadığı için AI testinin DirectML/Tensor Core yolu yalnızca derlendi; Wine'da "DirectML yok" yedek davranışı test edildi. Gerçek bir NVIDIA/AMD/Intel GPU'da [donanım test prosedürü](HARDWARE_TEST_PROCEDURE.md) adım 10 ile doğrulanmalıdır. DirectML başarısız olursa AI testi otomatik olarak shader FP32 yoluna düşer ve sonuçta hangi yolun kullanıldığı yazar.
12. **AI ölçümü bir modelin çıkarım hızı değildir.** Matris çarpımı verimini ölçer. Gerçek modellerin (ör. LLM token/sn) hızı bellek bant genişliği ve yazılım desteğine göre farklıdır.
13. **Teorik TOPS tablosu sınırlıdır.** NVIDIA RTX 20/30/40 ve AMD RX 7000 serisinin yaygın modellerini kapsar. RTX 50 serisi, Intel Arc ve dizüstü varyantları tabloda yoktur ve **N/A** gösterilir. Değerler üretici beyanıdır ve yaklaşıktır.
14. **Liderlik tablosu bir sunucu gerektirir.** Dağıtan kişinin bir Supabase projesi kurup `leaderboard.json` dosyasını pakete eklemesi gerekir ([kurulum](../server/leaderboard/README.md)). Sunucu şeması ve istemci, yerel PostgreSQL 16 + PostgREST 12 (Supabase ile aynı yapı) üzerinde test edildi; gerçek Supabase'de denenmedi.
15. **Hile önleme sınırlıdır.** Sunucu tutarlılık, hız ve kişisel rekor kurallarını uygular; ancak istemcide çalışan her benchmark gibi ağ isteği taklit edilerek sahte skor gönderilebilir. Şüpheli kayıtlar yönetici tarafından silinmelidir.
16. **İsim benzerliği.** "FurUltimate" adı, Geeks3D'nin "FurMark" ürününe benzer. Dağıtımdan önce marka ve isim hakları açısından değerlendirilmesi önerilir.
