#pragma once
struct TrustedAsset { const char* path; const char* sha256; };
inline constexpr TrustedAsset TrustedAssets[] = {
  {"ui/index.html", "6ed0e1125066dd26cfbe072040482def077161566806e4eb5a089c5f6a2194f9"},
  {"ui/app.js", "eb835a7b75a5847b02dde5f64d79808a9b7168a4b0ffabf0ffc0a50933f75125"},
  {"ui/style.css", "749541916e1b871707653517d06f2fe3f76b26534d7074aa7db714a232d6d2e0"},
  {"ui/help.js", "4e204d3eb9bf97c6c075d3969310fc21790b5ece603fcf2b99b8fc37de06dc3c"},
  {"ui/changelog.js", "7d8a8f50c4e557d630f2aab0af173ac89d742a9659cc0a0b6b46089b2b380491"},
  {"ui/assets/hero.png", "2861ea88c0b18020f4ff102dd39b7ed4ed640646d275cccbd2df12ac3ab678ae"},
  {"ui/assets/icon.png", "9f72b29d76e14e3892d36d163512453700bf17083fc8b48f791e0e0a2bd7e6f7"},
  {"ui/assets/app.ico", "3b077bf328df8e9972531624f401bf6cce826e62d6b620e75b4ffac7e665ad06"},
  {"ui/assets/hero-forest.png", "70309c30225ec2a9549ae45fd0ade82406d5a59d7d97f8ac888ba244eabf53ce"},
  {"ui/assets/hero-pass.png", "44e493968258bf08e12e3258ed93929f9af78273565dbbee565137a9d5e01dc4"},
  {"ui/assets/hero-outpost.png", "6c59d82467b99367b942b8f971242646724d70ae0fb2ea8c1766d7b7752a0feb"},
  {"ui/assets/hero-fogwood.png", "77160851e33ff8bddc79db52db8ae466c49b108b74230d999509e903b2e3b19d"},
  {"ui/assets/hero-tunnel.png", "e2e7770288262a9d88a642ef68673f94fe293253f9ae8a6aad66ebe4f8ad5c6a"},
  {"ui/assets/hero-lookout.png", "c07012dcc35d277565728df6cd837e368873a38d4aaa0bd5334cecd8ad07d365"},
  {"ui/assets/hero-sawmill.png", "9ee039474a87e00e9c4701729aac761d4a6ea652221ef9274aea97d1c8621887"},
  {"ui/assets/hero-lake.png", "092457b61ca25c3889ae728b4d83db703d50d94fae8a001d83b63357d024771a"},
  {"ui/assets/hero-quarantine.png", "df89b19c32631a454791e2f98e9f1b9865d7ca10b351a1a68117cb42c833228e"},
  {"ui/assets/hero-camp.png", "999f550e9ade3f3b47a4834b832b33be684f1d656684436b0de75ebeea83fe4e"},
  {"ui/assets/hero-burn.png", "bb463e05ed3816fd1d47a978025ed3bb96ef24083d9d4cac0c4cb66563818d03"},
  {"ui/assets/hero-freakers.png", "87412b71d9fa6714a65e62c014f177ac3a9041d456efa25f36f0328b90c3a493"},
  {"ui/assets/hero-breaker.png", "5606addf0e23bbfd6cc4f8a057920aa0a5c318c242298fc939f182c14689a300"},
  {"ui/assets/seasonal-emoji.svg", "6b7e5cd6b667e2b0f483c8492a48b3a266b1d4832eb9730985df48a4664cad68"},
  {"ui/assets/mod-art-the-clown.png", "ee252010935900f20a484362d9fef2ef8330507499e301523e66c59f1941e49b"},
  {"catalog/catalog.json", "a42f637efa102290d0934fd38183ca000b5c600b9bfa18f99666656b655a0ed2"},
  {"WebView2Loader.dll", "3426dcc55fdfb8b5e7ac623cf33b4ccf283fbf68a0c5a65bddfdb4d749a8abee"},
  {"config/nexus.json", "ef28a8177c7ad69df39ccad09885972271c6f1fbdf24cec06e1db04524c57f07"},
  {"DriftersToolkitSaveCodec.exe", "3dfa067798141d3641ecb898c8737e59d0b209bc3980069cb995d34502db868e"},
};
inline constexpr const char* SavePreviewHelperSha256 = "";
inline constexpr const char* SaveCodecHelperSha256 = "3dfa067798141d3641ecb898c8737e59d0b209bc3980069cb995d34502db868e";
