# Releasing / Sürüm yayımlama

The in-app updater reads `https://api.github.com/repos/eren-agalar/FurUltimate/releases/latest`. To ship an update, follow these steps.
Uygulama içi güncelleyici bu adresteki son sürümü okur. Güncelleme yayımlamak için şu adımları izleyin.

1. **Bump the version / Sürümü yükseltin:** `CMakeLists.txt` (`project(FurUltimate VERSION x.y.z)`) and `installer/furultimate.nsi` (`!define VERSION`). Add a `RELEASE_NOTES.md` entry.
2. **Build / Derleyin:** `./tools/build_release.sh`. The online leaderboard is embedded only if `leaderboard.json` exists at the repo root.
3. **Create a GitHub release / Release açın:**
   - **Tag / Etiket:** `vX.Y.Z`, higher than the previous one. Don't mark it as a pre-release; the updater ignores drafts and pre-releases.
   - **Assets / Dosyalar:** upload these with their names unchanged. The updater looks for them by name suffix.

     | File / Dosya | Used by / Kullanan |
     |---|---|
     | `FurUltimate-X.Y.Z-Setup.exe` | installed copies (silent `/S /UPDATE` install) / kurulu kopyalar |
     | `FurUltimate-X.Y.Z-win64.exe` | standalone / portable copies (self-replace) / tek exe ve taşınabilir kopyalar |
     | `SHA256SUMS.txt` | hash fallback when GitHub reports no asset digest / GitHub özet vermezse doğrulama |
     | `FurUltimate-X.Y.Z-win64-portable.zip` | manual download only / yalnızca elle indirme |

   - **Notes / Notlar:** write English first, then a `## Türkçe` section. Turkish users see the part after `## Türkçe`; others see the part before it.

If an asset is missing or its SHA-256 doesn't match, the updater changes nothing and points the user to the release page.
Dosya eksikse veya SHA-256 tutmazsa hiçbir şey değiştirilmez; kullanıcıya sürüm sayfası gösterilir.
