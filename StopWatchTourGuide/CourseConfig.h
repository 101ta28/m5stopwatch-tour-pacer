#pragma once
#include "src/Tour.h"

// Wi-Fi設定を保存するまでの初期値。名称はUTF-8、各目安は正の整数（秒）。
inline tour::Course configuredCourse() {
  return {"施設案内", {{"導入", 120}, {"展示説明", 420}, {"体験", 240}, {"移動", 120}}};
}
