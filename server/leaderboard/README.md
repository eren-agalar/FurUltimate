# FurUltimate Liderlik Sunucusu — Kurulum

Liderlik tablosu, ücretsiz katmanı olan **Supabase** (PostgreSQL + REST API) üzerinde çalışır. Kurulum yaklaşık 10 dakika sürer ve kod yazmayı gerektirmez.

## 1. Supabase projesi oluşturun
1. https://supabase.com adresinde ücretsiz hesap açın → **New project**.
2. Proje adı (ör. `furultimate`), güçlü bir veritabanı şifresi ve size yakın bir bölge (ör. Frankfurt) seçin.

## 2. Şemayı yükleyin
1. Sol menü → **SQL Editor** → **New query**.
2. Bu klasördeki `schema.sql` dosyasının tamamını yapıştırın → **Run**.
3. "Success" görmelisiniz. Betik tekrar çalıştırılabilir; sonraki sürümlerde güncelleme için de aynı şekilde kullanılır.

Bu betik şunları kurar:
- `leaderboard` tablosu. Oyuncu kimlikleri istemcilere kapalıdır.
- `leaderboard_public` ve `gpu_ranking` görünümleri. Bunlar herkese açık ve okuma amaçlıdır.
- `submit_score()` fonksiyonu. Uygulama yalnızca bu fonksiyon üzerinden yazabilir.
- `my_entries()` fonksiyonu. Oyuncu kendi kayıtlarını ve sırasını bununla görür.

## 3. `leaderboard.json` dosyasını oluşturun
Supabase → **Project Settings → API** sayfasından iki değeri alın:
- **Project URL** (ör. `https://abcdefgh.supabase.co`)
- **anon public** anahtarı (uzun bir `eyJ…` metni)

```json
{
  "url": "https://abcdefgh.supabase.co",
  "anonKey": "eyJhbGciOi..."
}
```

Dosyayı proje kökünde `leaderboard.json` adıyla kaydedin (git tarafından yok sayılır). CMake bu değerleri derleme sırasında `FurUltimate.exe`'nin içine gömer; böylece kurulum paketi, tek exe ve ZIP ek dosya olmadan sunucunuza bağlanır. Çalışma anında `%LOCALAPPDATA%\FurUltimate\leaderboard.json` veya exe'nin yanındaki bir `leaderboard.json`, gömülü değerlerin önüne geçer.

> **anon** anahtarının dağıtılması güvenlidir: bu anahtar yalnızca herkese açık görünümleri okuyabilir ve `submit_score()` fonksiyonunu çağırabilir. **service_role** anahtarını asla dağıtmayın.

## Sunucu tarafı kurallar (`submit_score`)
- **Kişisel rekor:** her oyuncu × pano × ekran kartı için tek kayıt tutulur. Yeni skor öncekinden yüksekse güncellenir ("improved"), değilse tutulur ("kept"). Uygulama da yalnızca yerel geçmişteki rekoru geçen skorları gönderir.
- **Panolar:** `overall:STANDARD-1080P`, `overall:HIGH-1440P`, `overall:ULTRA-2160P` ve `ai:DirectML FP16`, `ai:DirectML FP32`, `ai:Shader FP32`. Yalnızca tam (FULL) benchmark genel panolara girer; tek test ve doğrulama çalıştırmaları girmez. **Hızlı mod** ile alınan sonuçlar (genel veya AI) hiçbir panoya gönderilmez.
- **Tutarlılık:** genel skor, gönderilen beş alt skordan belgelenmiş ağırlıklı geometrik ortalamayla yeniden hesaplanır (±%1). AI skoru, TOPS değerinden yeniden hesaplanır.
- **Sınırlar:** kayıt güncellemeleri arasında en az 30 saniye olmalı; bir oyuncunun en fazla 50 kaydı olabilir; takma ad 2–20 karakter olmalı (harf, rakam, boşluk, `_ . -`, Türkçe karakterler).

### Hile önleme hakkında dürüst not
İstemci tarafında çalışan hiçbir benchmark tamamen hilesiz olamaz; kararlı biri ağ isteğini taklit edebilir. Bu kurallar sahte skorları zorlaştırır ama imkânsız kılmaz. Şüpheli kayıtları Supabase → **Table Editor → leaderboard** üzerinden silebilirsiniz:

```sql
delete from public.leaderboard where nickname = 'şüpheli_ad';
```

## Maliyet
Supabase ücretsiz katmanı (500 MB veritabanı) yüz binlerce kayıt için yeterlidir. Her kayıt yaklaşık 0,5 KB'tır.

## Yerel test (geliştiriciler için)
Geliştirme sırasında aynı şema yerel PostgreSQL 16 + PostgREST 12 ile test edilmiştir. Supabase'deki `anon` rolü ve `/rest/v1` yolu birebir taklit edilmiştir. İstemci entegrasyon testini çalıştırmak için:

```bash
FNM_LB_TEST_URL=http://127.0.0.1:3001 FNM_LB_TEST_KEY=<anon-jwt> wine build/aether_tests.exe leaderboard_client
```
