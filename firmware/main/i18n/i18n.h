#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LANG_EN = 0,
    LANG_ZH_CN,
    LANG_ZH_TW,
    LANG_JA,
    LANG_COUNT,
    LANG_UNSET = 0xFF,
} lang_t;

/* X(id, en, zh_CN, zh_TW, ja) */
#define STR_TABLE(X) \
X(APP_TITLE,        "Clientre 5",              "Clientre 5",          "Clientre 5",          "Clientre 5") \
X(OK,               "OK",                        "确定",                  "確定",                  "OK") \
X(CANCEL,           "Cancel",                    "取消",                  "取消",                  "キャンセル") \
X(YES,              "Yes",                       "是",                    "是",                    "はい") \
X(NO,               "No",                        "否",                    "否",                    "いいえ") \
X(BACK,             "Back",                      "返回",                  "返回",                  "戻る") \
X(SAVE,             "Save",                      "保存",                  "儲存",                  "保存") \
X(DELETE,           "Delete",                    "删除",                  "刪除",                  "削除") \
X(EDIT,             "Edit",                      "编辑",                  "編輯",                  "編集") \
X(CONNECT,          "Connect",                   "连接",                  "連接",                  "接続") \
X(PLAY,             "Play",                      "播放",                  "播放",                  "再生") \
X(PAUSE,            "Pause",                     "暂停",                  "暫停",                  "一時停止") \
X(TERM_MENU,        "Terminal",                  "终端",                  "終端",                  "ターミナル") \
X(TERM_CLEAR,       "Clear the screen",          "清屏",                  "清除畫面",              "画面をクリア") \
X(TERM_SCROLL_END,  "Jump to the newest line",   "跳到最新一行",          "跳到最新一行",          "最新行へ移動") \
X(GESTURE_NOTE,     "Swipe right to go back. Long-press a tile for its shortcut.", "右滑返回，长按磁贴使用快捷操作。", "右滑返回，長按磁磚使用快捷操作。", "右スワイプで戻る。タイル長押しでショートカット。") \
X(SET_SECURITY,     "Security",                  "安全",                  "安全",                  "セキュリティ") \
X(CREDS_ON_SD,      "Keep SSH servers on the TF card", "SSH 服务器保存到 TF 卡", "SSH 伺服器儲存到 TF 卡", "SSH サーバを TF カードに保存") \
X(CREDS_NOTE,       "Hosts, user names and passwords are encrypted with the device key and written to the card; nothing is left in flash. The key stays on the device, so the card alone cannot be read. Without the card the list starts empty.", "主机、用户名和密码用设备密钥加密后写入卡中，闪存不留数据。密钥保存在设备上，因此仅有卡无法读取。没有卡时列表为空。", "主機、使用者名稱與密碼以裝置金鑰加密後寫入卡片，快閃記憶體不留資料。金鑰保存在裝置上，因此僅有卡片無法讀取。沒有卡片時清單為空。", "ホスト名・ユーザー名・パスワードを端末鍵で暗号化してカードに書き込み、フラッシュには何も残しません。鍵は端末側に残るため、カードだけでは復号できません。カードが無い場合、一覧は空で起動します。") \
X(CREDS_RELOAD,     "Re-read from the card",     "从卡中重新读取",        "從卡片重新讀取",        "カードから読み直す") \
X(CREDS_NO_SD,      "The TF card is not available", "TF 卡不可用",        "TF 卡無法使用",         "TF カードを利用できません") \
X(DISCONNECT,       "Disconnect",                "断开",                  "斷開",                  "切断") \
X(CLOSE,            "Close",                     "关闭",                  "關閉",                  "閉じる") \
X(DONE,             "Done",                      "完成",                  "完成",                  "完了") \
X(PLEASE_WAIT,      "Please wait...",            "请稍候...",             "請稍候...",             "お待ちください...") \
X(SERVER_SAVE_FAILED, "Could not save the connection. Check storage and the server limit.", "无法保存连接。请检查存储空间和连接数量。", "無法儲存連線。請檢查儲存空間和連線數量。", "接続を保存できませんでした。保存先と登録件数を確認してください。") \
X(ERROR,            "Error",                     "错误",                  "錯誤",                  "エラー") \
X(FAILED,           "Failed",                    "失败",                  "失敗",                  "失敗") \
X(BAD_COLOR,        "Use #RRGGBB",               "请输入 #RRGGBB",        "請輸入 #RRGGBB",        "#RRGGBB の形式で入力してください") \
X(SUCCESS,          "Success",                   "成功",                  "成功",                  "成功") \
X(ON,               "On",                        "开",                    "開",                    "オン") \
X(OFF,              "Off",                       "关",                    "關",                    "オフ") \
X(NONE,             "None",                      "无",                    "無",                    "なし") \
X(UNKNOWN,          "Unknown",                   "未知",                  "未知",                  "不明") \
X(SELECT_LANGUAGE,  "Select Language",           "选择语言",              "選擇語言",              "言語を選択") \
X(LANG_CHANGE_NOTE, "The device will reboot to apply the language.", "设备将重启以应用语言设置。", "裝置將重新啟動以套用語言設定。", "言語を適用するため再起動します。") \
X(HOME_SSH,         "SSH Terminal",              "SSH 终端",              "SSH 終端",              "SSH ターミナル") \
X(HOME_FILES,       "File Manager",              "文件管理",              "檔案管理",              "ファイル管理") \
X(HOME_MUSIC,       "Music",                     "音乐",                  "音樂",                  "音楽") \
X(HOME_CAMERA,      "Camera",                    "相机",                  "相機",                  "カメラ") \
X(HOME_WIFI,        "WiFi",                      "WiFi",                  "WiFi",                  "WiFi") \
X(HOME_SETTINGS,    "Settings",                  "设置",                  "設定",                  "設定") \
X(HOME_WEBFM,       "Web Files",                 "在线文件",              "線上檔案",              "Web ファイル") \
X(HOME_DEVINFO,     "Device Info",               "设备信息",              "裝置資訊",              "デバイス情報") \
X(HOME_USB,         "USB",                       "USB",                   "USB",                   "USB") \
X(HOME_TOOLS,       "Tools",                     "工具",                  "工具",                  "ツール") \
X(HOME_OTA,         "Firmware",                  "固件",                  "韌體",                  "ファームウェア") \
X(HOME_SCREENSAVER, "Screensaver",               "屏保",                  "螢幕保護",              "スクリーンセーバー") \
X(CAMERA_STARTING,  "Starting camera...",         "正在启动相机...",        "正在啟動相機...",        "カメラを起動しています...") \
X(CAMERA_READY,     "Ready",                     "就绪",                  "就緒",                  "撮影できます") \
X(CAMERA_CAPTURE,   "Take photo",                "拍照",                  "拍照",                  "撮影") \
X(CAMERA_SAVING,    "Saving photo...",           "正在保存照片...",        "正在儲存照片...",        "写真を保存しています...") \
X(CAMERA_SAVED,     "Saved to TF/DCIM/Clientre5", "已保存到 TF/DCIM/Clientre5", "已儲存至 TF/DCIM/Clientre5", "TF/DCIM/Clientre5 に保存しました") \
X(CAMERA_FAILED,    "Camera could not start",    "无法启动相机",           "無法啟動相機",           "カメラを起動できません") \
X(CAMERA_SAVE_FAIL, "Could not save photo",      "无法保存照片",           "無法儲存照片",           "写真を保存できません") \
X(SERVERS,          "SSH Servers",               "SSH 服务器",            "SSH 伺服器",            "SSH サーバー") \
X(ADD_SERVER,       "Add Server",                "添加服务器",            "新增伺服器",            "サーバーを追加") \
X(EDIT_SERVER,      "Edit Server",               "编辑服务器",            "編輯伺服器",            "サーバーを編集") \
X(DEL_SERVER,       "Delete Server",             "删除服务器",            "刪除伺服器",            "サーバーを削除") \
X(DEL_SERVER_Q,     "Delete this server?",       "删除此服务器？",        "刪除此伺服器？",        "このサーバーを削除しますか？") \
X(NO_SERVERS,       "No servers yet. Tap + to add one.", "还没有服务器，点击 + 添加。", "尚無伺服器，點擊 + 新增。", "サーバーがありません。+ で追加してください。") \
X(NAME,             "Name",                      "名称",                  "名稱",                  "名前") \
X(HOST,             "Host / IP",                 "主机 / IP",             "主機 / IP",             "ホスト / IP") \
X(PORT,             "Port",                      "端口",                  "連接埠",                "ポート") \
X(USERNAME,         "Username",                  "用户名",                "使用者名稱",            "ユーザー名") \
X(PASSWORD,         "Password",                  "密码",                  "密碼",                  "パスワード") \
X(AUTH_METHOD,      "Auth",                      "认证方式",              "驗證方式",              "認証") \
X(AUTH_PASSWORD,    "Password",                  "密码",                  "密碼",                  "パスワード") \
X(AUTH_KEY,         "Private key (SD)",          "私钥 (SD)",             "私鑰 (SD)",             "秘密鍵 (SD)") \
X(KEY_PATH,         "Key file path",             "私钥路径",              "私鑰路徑",              "鍵ファイルのパス") \
X(HOST_USER_REQ,    "Host and user required.",   "主机和用户名必填。",    "主機與使用者名稱必填。","ホストとユーザー名は必須です。") \
X(SAVED,            "Saved.",                    "已保存。",              "已儲存。",              "保存しました。") \
X(CONNECTING,       "Connecting...",             "正在连接...",           "正在連接...",           "接続中...") \
X(CONNECTED,        "Connected",                 "已连接",                "已連接",                "接続済み") \
X(DISCONNECTED,     "Disconnected",              "已断开",                "已斷開",                "切断されました") \
X(SSH_CONN_FAILED,  "SSH connection failed.",    "SSH 连接失败。",        "SSH 連接失敗。",        "SSH 接続に失敗しました。") \
X(SSH_AUTH_FAILED,  "Authentication failed.",    "认证失败。",            "驗證失敗。",            "認証に失敗しました。") \
X(SSH_HOSTKEY_NEW,  "New host key. Trust and continue?", "新的主机密钥，是否信任并继续？", "新的主機金鑰，是否信任並繼續？", "新しいホスト鍵です。信頼して続行しますか？") \
X(SSH_HOSTKEY_CHANGED, "HOST KEY CHANGED! Possible attack. Continue?", "主机密钥已更改！可能存在攻击，是否继续？", "主機金鑰已變更！可能存在攻擊，是否繼續？", "ホスト鍵が変更されました。攻撃の可能性があります。続行しますか？") \
X(WIFI_NOT_CONNECTED, "WiFi not connected",      "WiFi 未连接",           "WiFi 未連接",           "WiFi 未接続") \
X(TERM_CLOSE_Q,     "Disconnect SSH and return?", "断开 SSH 并返回？",    "斷開 SSH 並返回？",     "SSH を切断して戻りますか？") \
X(KEYBOARD,         "Keyboard",                  "键盘",                  "鍵盤",                  "キーボード") \
X(PHYS_KBD_DETECTED,"Physical keyboard detected", "已检测到物理键盘",     "已偵測到實體鍵盤",      "物理キーボードを検出") \
X(IME_MODE,         "Input Source",              "输入法",                "輸入法",                "入力ソース") \
X(IME_EN,           "English",                   "英文",                  "英文",                  "英語") \
X(IME_JA,           "Japanese",                  "日文",                  "日文",                  "日本語") \
X(IME_ZH,           "Chinese (Pinyin)",          "中文 (拼音)",           "中文 (拼音)",           "中国語 (ピンイン)") \
X(IME_HIRA,         "Hiragana",                  "平假名",                "平假名",                "ひらがな") \
X(IME_KATA,         "Katakana",                  "片假名",                "片假名",                "カタカナ") \
X(IME_SWITCH_KEY,   "Switch key: Ctrl+Space",    "切换键：Ctrl+Space",    "切換鍵：Ctrl+Space",    "切替キー: Ctrl+Space") \
X(IME_SKK_DICT,     "SKK dictionary",            "SKK 词典",              "SKK 詞典",              "SKK 辞書") \
X(IME_DICT_BUILTIN, "Built-in (M)",              "内置 (M)",              "內建 (M)",              "内蔵 (M)") \
X(IME_DICT_SD,      "SD: /skk/SKK-JISYO.L",      "SD: /skk/SKK-JISYO.L",  "SD: /skk/SKK-JISYO.L",  "SD: /skk/SKK-JISYO.L") \
X(IME_DICT_LOADED,  "Dictionary loaded",         "词典已加载",            "詞典已載入",            "辞書を読み込みました") \
X(IME_NO_CAND,      "No candidates",             "无候选",                "無候選",                "候補なし") \
X(FILES,            "Files",                     "文件",                  "檔案",                  "ファイル") \
X(TF_CARD,          "TF Card",                   "TF 卡",                 "TF 卡",                 "TF カード") \
X(USB_DRIVE,        "USB Drive",                 "U盘",                   "USB 隨身碟",            "USB メモリ") \
X(TF_NOT_MOUNTED,   "TF card not mounted",       "TF 卡未挂载",           "TF 卡未掛載",           "TF カードが未マウント") \
X(EMPTY_FOLDER,     "Empty folder",              "空文件夹",              "空資料夾",              "空のフォルダ") \
X(COPY,             "Copy",                      "复制",                  "複製",                  "コピー") \
X(CUT,              "Cut",                       "剪切",                  "剪下",                  "切り取り") \
X(PASTE,            "Paste",                     "粘贴",                  "貼上",                  "貼り付け") \
X(RENAME,           "Rename",                    "重命名",                "重新命名",              "名前を変更") \
X(NEW_FOLDER,       "New Folder",                "新建文件夹",            "新增資料夾",            "新規フォルダ") \
X(NEW_FILE,         "New File",                  "新建文件",              "新增檔案",              "新規ファイル") \
X(SELECT,           "Select",                    "选择",                  "選擇",                  "選択") \
X(SELECT_ALL,       "Select All",                "全选",                  "全選",                  "すべて選択") \
X(CANCEL_SELECT,    "Cancel Select",             "取消选择",              "取消選擇",              "選択解除") \
X(DELETE_SELECTED,  "Delete Selected",           "删除所选",              "刪除所選",              "選択項目を削除") \
X(DELETE_Q,         "Delete %d item(s)? This cannot be undone.", "删除 %d 项？此操作不可撤销。", "刪除 %d 項？此操作無法復原。", "%d 項目を削除しますか？元に戻せません。") \
X(CLIPBOARD_EMPTY,  "Clipboard is empty",        "剪贴板为空",            "剪貼簿為空",            "クリップボードが空です") \
X(PASTE_DONE,       "Paste done",                "粘贴完成",              "貼上完成",              "貼り付け完了") \
X(COPYING,          "Copying %s",                "正在复制 %s",           "正在複製 %s",           "コピー中 %s") \
X(OP_FAILED,        "Operation failed: %s",      "操作失败：%s",          "操作失敗：%s",          "操作に失敗: %s") \
X(TARGET_EXISTS,    "Target exists",             "目标已存在",            "目標已存在",            "同名の項目があります") \
X(INTO_ITSELF,      "Cannot move into itself",   "不能移动到自身",        "不能移動到自身",        "自分自身へは移動できません") \
X(FILE_NAME,        "File name",                 "文件名",                "檔案名稱",              "ファイル名") \
X(FOLDER_NAME,      "Folder name",               "文件夹名",              "資料夾名稱",            "フォルダ名") \
X(ITEMS_TOTAL,      "%u items, %s",              "%u 项，总大小 %s",      "%u 項，總大小 %s",      "%u 項目、合計 %s") \
X(FREE_SPACE,       "Free %s / %s",              "可用 %s / %s",          "可用 %s / %s",          "空き %s / %s") \
X(OPEN_WITH,        "Open",                      "打开",                  "開啟",                  "開く") \
X(IMAGE_VIEWER,     "Image Viewer",              "图片查看",              "圖片檢視",              "画像ビューア") \
X(TEXT_VIEWER,      "Text Viewer",               "文本查看",              "文字檢視",              "テキストビューア") \
X(FILE_TOO_LARGE,   "File too large",            "文件过大",              "檔案過大",              "ファイルが大きすぎます") \
X(CANNOT_READ,      "Cannot read file.",         "无法读取文件。",        "無法讀取檔案。",        "ファイルを読めません。") \
X(LOADING_IMAGE,    "Loading image...",          "正在加载图片...",       "正在載入圖片...",       "画像を読み込み中...") \
X(IMAGE_LOAD_FAILED,"Image load failed",         "图片加载失败",          "圖片載入失敗",          "画像の読み込みに失敗") \
X(EXIF_INFO,        "EXIF",                      "EXIF",                  "EXIF",                  "EXIF") \
X(NO_EXIF,          "No EXIF data",              "无 EXIF 信息",          "無 EXIF 資訊",          "EXIF 情報なし") \
X(ROTATE,           "Rotate",                    "旋转",                  "旋轉",                  "回転") \
X(MUSIC_PLAYER,     "Music Player",              "音乐播放器",            "音樂播放器",            "音楽プレーヤー") \
X(NOW_PLAYING,      "Now Playing",               "正在播放",              "正在播放",              "再生中") \
X(FAVORITES,        "Favorites",                 "收藏",                  "收藏",                  "お気に入り") \
X(ALL_MUSIC,        "All Music",                 "全部音乐",              "全部音樂",              "すべての音楽") \
X(ADD_FAV,          "Add to Favorites",          "加入收藏",              "加入收藏",              "お気に入りに追加") \
X(REMOVE_FAV,       "Remove from Favorites",     "取消收藏",              "取消收藏",              "お気に入りから削除") \
X(ADDED_FAV,        "Added to favorites",        "已加入收藏",            "已加入收藏",            "お気に入りに追加しました") \
X(REMOVED_FAV,      "Removed from favorites",    "已取消收藏",            "已取消收藏",            "お気に入りから削除しました") \
X(CLEAR_FAV,        "Clear Favorites",           "清空收藏",              "清空收藏",              "お気に入りを消去") \
X(SCANNING_AUDIO,   "Scanning audio files...",   "正在扫描音频...",       "正在掃描音訊...",       "音楽ファイルを検索中...") \
X(NO_AUDIO,         "No audio files found",      "未找到音频文件",        "未找到音訊檔案",        "音楽ファイルがありません") \
X(DECODE_FAILED,    "Decode failed",             "解码失败",              "解碼失敗",              "デコードに失敗") \
X(VOLUME,           "Volume",                    "音量",                  "音量",                  "音量") \
X(MUSIC_STOPPED,    "Music stopped",             "音乐已停止",            "音樂已停止",            "音楽を停止しました") \
X(WIFI_MANAGER,     "WiFi Manager",              "WiFi 管理",             "WiFi 管理",             "WiFi マネージャー") \
X(WIFI_STATUS,      "Status",                    "状态",                  "狀態",                  "状態") \
X(WIFI_SCAN,        "Scan",                      "扫描",                  "掃描",                  "スキャン") \
X(WIFI_SCANNING,    "Scanning...",               "正在扫描...",           "正在掃描...",           "スキャン中...") \
X(WIFI_SAVED,       "Saved Networks",            "已保存网络",            "已儲存網路",            "保存済みネットワーク") \
X(WIFI_MANUAL,      "Manual WiFi",               "手动输入",              "手動輸入",              "手動入力") \
X(WIFI_SSID,        "SSID",                      "SSID",                  "SSID",                  "SSID") \
X(WIFI_SAVE_CONNECT,"Save & Connect",            "保存并连接",            "儲存並連接",            "保存して接続") \
X(WIFI_FORGET,      "Forget",                    "删除",                  "刪除",                  "削除") \
X(WIFI_OPEN_NOTE,   "Leave the password blank for open networks.", "开放网络可留空密码。", "開放網路可留空密碼。", "オープンネットワークではパスワードを空にしてください。") \
X(WIFI_CONNECT_FAILED, "Connect failed. Check password.", "连接失败，请检查密码。", "連接失敗，請檢查密碼。", "接続に失敗しました。パスワードを確認してください。") \
X(WIFI_SAVED_DELETED, "Saved WiFi deleted.",     "已删除保存的 WiFi。",   "已刪除儲存的 WiFi。",   "保存した WiFi を削除しました。") \
X(WIFI_ENABLE,      "WiFi enabled",              "WiFi 已开启",           "WiFi 已開啟",           "WiFi 有効") \
X(IPV4,             "IPv4",                      "IPv4",                  "IPv4",                  "IPv4") \
X(IPV6,             "IPv6",                      "IPv6",                  "IPv6",                  "IPv6") \
X(RSSI,             "Signal",                    "信号",                  "訊號",                  "電波強度") \
X(CHANNEL,          "Channel",                   "信道",                  "頻道",                  "チャンネル") \
X(POWER_NOTE,       "WiFi upload draws up to 11W. Use a 5V 2A adapter.", "WiFi 上传功耗可达 11W，请使用 5V 2A 适配器。", "WiFi 上傳功耗可達 11W，請使用 5V 2A 變壓器。", "WiFi アップロード時は最大 11W 消費します。5V 2A のアダプタを使用してください。") \
X(SETTINGS,         "Settings",                  "设置",                  "設定",                  "設定") \
X(SET_SYSTEM,       "System",                    "系统",                  "系統",                  "システム") \
X(SET_DISPLAY,      "Display",                   "显示",                  "顯示",                  "画面") \
X(SET_INPUT,        "Input",                     "输入",                  "輸入",                  "入力") \
X(SET_BACKUP,       "Backup",                    "备份",                  "備份",                  "バックアップ") \
X(SET_LANGUAGE,     "Language",                  "语言",                  "語言",                  "言語") \
X(SET_BRIGHTNESS,   "Brightness",                "亮度",                  "亮度",                  "明るさ") \
X(SET_ROTATION,     "Screen rotation",           "屏幕方向",              "螢幕方向",              "画面の向き") \
X(SET_TIME,         "Time",                      "时间",                  "時間",                  "時刻") \
X(SET_UTC_OFFSET,   "UTC offset",                "UTC 偏移",              "UTC 偏移",              "UTC オフセット") \
X(SET_UTC_NOTE,     "UTC+ means local time is ahead of UTC. Japan is UTC+9.", "UTC+ 表示当地时间比 UTC 快；日本为 UTC+9。", "UTC+ 表示當地時間比 UTC 快；日本為 UTC+9。", "UTC+ は現地時間が UTC より進んでいることを表します。日本は UTC+9 です。") \
X(SET_NTP,          "Sync time (NTP)",           "网络对时 (NTP)",        "網路對時 (NTP)",        "時刻同期 (NTP)") \
X(SET_TIME_SYNCED,  "Time synchronized",         "时间已同步",            "時間已同步",            "時刻を同期しました") \
X(SET_SCREENSHOT_NOTE, "Three-finger tap saves a screenshot to SD/ScreenShots.", "三指点击将截图保存到 SD/ScreenShots。", "三指點擊將截圖儲存到 SD/ScreenShots。", "3 本指タップで SD/ScreenShots にスクリーンショットを保存します。") \
X(SCREENSHOT_SAVED, "Saved to SD/ScreenShots",   "已保存到 SD/ScreenShots", "已儲存到 SD/ScreenShots", "SD/ScreenShots に保存しました") \
X(SCREENSHOT_FAILED,"Screenshot failed",         "截图失败",              "截圖失敗",              "スクリーンショットに失敗") \
X(SET_SCREENSAVER,  "Screensaver",               "屏保",                  "螢幕保護",              "スクリーンセーバー") \
X(SET_STATUSBAR,    "Status bar",                "状态栏",                "狀態列",                "ステータスバー") \
X(SB_WIFI,          "WiFi icon",                 "WiFi 图标",             "WiFi 圖示",             "WiFi アイコン") \
X(SB_TAILSCALE,     "Tailscale icon",            "Tailscale 图标",        "Tailscale 圖示",        "Tailscale アイコン") \
X(SB_IP,            "IP address",                "IP 地址",               "IP 位址",               "IP アドレス") \
X(SB_BATTERY,       "Battery",                   "电池",                  "電池",                  "バッテリー") \
X(SB_KEYBOARD,      "Keyboard icon",             "键盘图标",              "鍵盤圖示",              "キーボードアイコン") \
X(SB_CLOCK,         "Clock",                     "时钟",                  "時鐘",                  "時刻") \
X(SB_DATE,          "Date",                      "日期",                  "日期",                  "日付") \
X(SB_YEAR,          "Year in the date",          "日期含年份",            "日期含年份",            "日付に年を含める") \
X(SB_NOTE,          "Icons blink while the link is coming up.", "连接中图标会闪烁。", "連線中圖示會閃爍。", "接続中はアイコンが点滅します。") \
X(SET_THEME,        "Appearance",                "外观",                  "外觀",                  "外観") \
X(TH_PRESET,        "Theme",                     "主题",                  "主題",                  "テーマ") \
X(TH_CUSTOM,        "Custom",                    "自定义",                "自訂",                  "カスタム") \
X(TH_RADIUS,        "Corner rounding",           "圆角",                  "圓角",                  "角の丸み") \
X(TH_DENSITY,       "Spacing",                   "间距",                  "間距",                  "余白") \
X(TH_COMPACT,       "Compact",                   "紧凑",                  "緊湊",                  "狭い") \
X(TH_NORMAL,        "Normal",                    "标准",                  "標準",                  "標準") \
X(TH_ROOMY,         "Roomy",                     "宽松",                  "寬鬆",                  "広い") \
X(TH_TILE_ACCENT,   "Tint home tiles with the accent", "主屏图标使用强调色", "主畫面圖示使用強調色", "ホームのタイルをアクセント色にする") \
X(TH_GLOW,          "Glow behind cards",         "卡片发光",              "卡片發光",              "カードを発光させる") \
X(TH_SCANLINES,     "Scan lines",                "扫描线",                "掃描線",                "走査線") \
X(TH_COLORS,        "Colours",                   "颜色",                  "顏色",                  "色") \
X(TH_BG,            "Background",                "背景",                  "背景",                  "背景") \
X(TH_CARD,          "Card",                      "卡片",                  "卡片",                  "カード") \
X(TH_CARD_HI,       "Card highlight",            "卡片高亮",              "卡片高亮",              "カード強調") \
X(TH_ACCENT,        "Accent",                    "强调色",                "強調色",                "アクセント") \
X(TH_ACCENT2,       "Secondary accent",          "次强调色",              "次強調色",              "サブアクセント") \
X(TH_DANGER,        "Danger",                    "危险",                  "危險",                  "警告") \
X(TH_TEXT,          "Text",                      "文字",                  "文字",                  "文字") \
X(TH_MUTED,         "Secondary text",            "次要文字",              "次要文字",              "補助文字") \
X(TH_TERMINAL,      "Terminal",                  "终端",                  "終端",                  "ターミナル") \
X(TH_TERM_FG,       "Foreground",                "前景",                  "前景",                  "前景") \
X(TH_TERM_BG,       "Background",                "背景",                  "背景",                  "背景") \
X(TH_TERM_CURSOR,   "Cursor",                    "光标",                  "游標",                  "カーソル") \
X(TH_CURSOR_STYLE,  "Cursor shape",              "光标形状",              "游標形狀",              "カーソル形状") \
X(TH_CUR_BLOCK,     "Block",                     "方块",                  "方塊",                  "ブロック") \
X(TH_CUR_UNDER,     "Underline",                 "下划线",                "底線",                  "下線") \
X(TH_CUR_BAR,       "Bar",                       "竖线",                  "豎線",                  "縦線") \
X(TH_CURSOR_BLINK,  "Blink the cursor",          "光标闪烁",              "游標閃爍",              "カーソルを点滅させる") \
X(TH_LINE_HEIGHT,   "Line height",               "行高",                  "行高",                  "行の高さ") \
X(TH_ANSI,          "ANSI palette",              "ANSI 调色板",           "ANSI 調色盤",           "ANSI パレット") \
X(TH_RESET,         "Reset to the preset",       "恢复预设",              "恢復預設",              "プリセットに戻す") \
X(SS_CLASSIC, "Classic movable layout", "经典可移动布局", "經典可移動版面", "従来の自由配置レイアウト") \
X(SS_EXIT_HINT, "Touch or press a key to return", "触摸或按键返回", "觸碰或按鍵返回", "タッチまたはキー入力で戻る") \
X(SS_ENABLE,        "Enable screensaver",        "启用屏保",              "啟用螢幕保護",          "スクリーンセーバーを有効化") \
X(SS_IDLE_MIN,      "Idle time (min)",           "空闲时间 (分钟)",       "閒置時間 (分鐘)",       "待機時間 (分)") \
X(SS_WALLPAPER_DIR, "Wallpaper folder",          "壁纸文件夹",            "桌布資料夾",            "壁紙フォルダ") \
X(SS_SWITCH_MIN,    "Switch every (min)",        "切换间隔 (分钟)",       "切換間隔 (分鐘)",       "切替間隔 (分)") \
X(SS_DISABLE_IN_SSH,"Disable during SSH sessions", "SSH 会话中禁用屏保",  "SSH 會話中停用螢幕保護","SSH 接続中は無効") \
X(SS_START_NOW,     "Start now",                 "立即启动",              "立即啟動",              "今すぐ開始") \
X(SS_MOVE_HINT,     "Long-press a widget to move it. Tap to exit.", "长按小部件可移动，点击退出。", "長按小工具可移動，點擊退出。", "ウィジェットを長押しで移動、タップで終了。") \
X(SS_LAYOUT_RESET,  "Reset layout",              "重置布局",              "重設版面",              "配置をリセット") \
X(SS_SAVED,         "Screensaver settings saved.", "屏保设置已保存。",    "螢幕保護設定已儲存。",  "スクリーンセーバー設定を保存しました。") \
X(SS_SELECT_FOLDER, "Please select a wallpaper folder.", "请选择壁纸文件夹。", "請選擇桌布資料夾。", "壁紙フォルダを選択してください。") \
X(SET_KBD_LED,      "Keyboard LED",              "键盘灯",                "鍵盤燈",                "キーボード LED") \
X(SET_KBD_LED_BRIGHT, "LED brightness",          "灯光亮度",              "燈光亮度",              "LED の明るさ") \
X(SET_KBD_STATUS,   "Keyboard: %s",              "键盘：%s",              "鍵盤：%s",              "キーボード: %s") \
X(SET_KBD_LAYOUT,   "Keyboard layout",           "键盘布局",              "鍵盤配置",              "キーボード配列") \
X(KBD_LAYOUT_STOCK, "As printed",                "按键帽印刷",            "依鍵帽印刷",            "刻印どおり") \
X(KBD_LAYOUT_JIS,   "JIS (Japanese)",            "JIS（日语）",           "JIS（日文）",           "JIS（日本語）") \
X(SET_KBD_LAYOUT_NOTE, "JIS reads the raw key matrix and maps the layout on this device, so the keys no longer match the printed caps. Fn gives F1-F12 on the number row.", "JIS 会读取原始按键矩阵并在本机重新映射，因此按键与键帽印刷不一致。Fn + 数字行为 F1-F12。", "JIS 會讀取原始按鍵矩陣並在本機重新對應，因此按鍵與鍵帽印刷不一致。Fn + 數字列為 F1-F12。", "JIS はキーマトリクスを直接読み、配列を本体側で割り当て直します。刻印とは一致しません。Fn+数字段で F1-F12 です。") \
X(PRESENT,          "present",                   "已连接",                "已連接",                "接続中") \
X(ABSENT,           "not detected",              "未检测到",              "未偵測到",              "未検出") \
X(SET_TERM_FONT,    "Terminal font size",        "终端字号",              "終端字型大小",          "ターミナルの文字サイズ") \
X(BACKUP_EXPORT_ENC,"Export Encrypted",          "导出加密备份",          "匯出加密備份",          "暗号化して書き出し") \
X(BACKUP_EXPORT_PLAIN, "Export Plain",           "导出明文备份",          "匯出明文備份",          "平文で書き出し") \
X(BACKUP_IMPORT,    "Import from SD",            "从 SD 导入",            "從 SD 匯入",            "SD から読み込み") \
X(BACKUP_PASSWORD,  "Backup password",           "备份密码",              "備份密碼",              "バックアップのパスワード") \
X(BACKUP_PW_NOTE,   "Enter 4-64 letters/digits. This password protects SSH/WiFi passwords.", "输入 4-64 位字母/数字，用于保护 SSH/WiFi 密码。", "輸入 4-64 位字母/數字，用於保護 SSH/WiFi 密碼。", "4〜64 文字の英数字を入力してください。SSH/WiFi のパスワードを保護します。") \
X(BACKUP_WRITTEN,   "Backup written to %s",      "备份已写入 %s",         "備份已寫入 %s",         "バックアップを %s に書き出しました") \
X(BACKUP_IMPORTED,  "Backup imported. Rebooting...", "备份已导入，正在重启...", "備份已匯入，正在重新啟動...", "バックアップを読み込みました。再起動します...") \
X(BACKUP_NOT_FOUND, "No backup found on SD.",    "SD 卡上没有备份。",     "SD 卡上沒有備份。",     "SD にバックアップがありません。") \
X(BACKUP_ENC_FOUND, "Encrypted backup found. Enter password to import.", "发现加密备份，请输入密码导入。", "發現加密備份，請輸入密碼匯入。", "暗号化バックアップがあります。パスワードを入力してください。") \
X(BACKUP_BAD_PW,    "Wrong password or damaged data", "密码错误或数据损坏", "密碼錯誤或資料損壞",   "パスワードが違うかデータが破損しています") \
X(BACKUP_PLAIN_WARN,"Plain backups contain passwords in clear text.", "明文备份包含明文密码。", "明文備份包含明文密碼。", "平文バックアップにはパスワードがそのまま含まれます。") \
X(WEBFM,            "Web File Manager",          "在线文件管理",          "線上檔案管理",          "Web ファイルマネージャー") \
X(WEBFM_START,      "Start",                     "启动",                  "啟動",                  "開始") \
X(WEBFM_STOP,       "Stop",                      "停止",                  "停止",                  "停止") \
X(WEBFM_RUNNING,    "Running at http://%s/",     "运行中：http://%s/",    "運行中：http://%s/",    "http://%s/ で稼働中") \
X(WEBFM_STOPPED,    "Stopped",                   "已停止",                "已停止",                "停止中") \
X(WEBFM_NEED_WIFI,  "Connect WiFi first.",       "请先连接 WiFi。",       "請先連接 WiFi。",       "先に WiFi に接続してください。") \
X(WEBFM_NOTE,       "Uploading many large files over WiFi is unstable (SDIO flow control).", "通过 WiFi 上传多个大文件不稳定 (SDIO 流控)。", "透過 WiFi 上傳多個大檔案不穩定 (SDIO 流控)。", "WiFi 経由で大きなファイルを多数アップロードすると不安定になります (SDIO フロー制御)。") \
X(OTA_TITLE,        "Firmware Update",           "固件升级",              "韌體升級",              "ファームウェア更新") \
X(OTA_CURRENT,      "Current: %s",               "当前：%s",              "目前：%s",              "現在: %s") \
X(OTA_LATEST,       "Latest: %s",                "最新：%s",              "最新：%s",              "最新: %s") \
X(OTA_CHECK,        "Check Update",              "检查更新",              "檢查更新",              "更新を確認") \
X(OTA_CHECKING,     "Checking update...",        "正在检查更新...",       "正在檢查更新...",       "更新を確認中...") \
X(OTA_LATEST_ALREADY, "Already latest.",         "已是最新版本。",        "已是最新版本。",        "すでに最新です。") \
X(OTA_FOUND,        "Update found",              "发现更新",              "發現更新",              "更新があります") \
X(OTA_DOWNLOAD,     "Download to SD",            "下载到 SD",             "下載到 SD",             "SD にダウンロード") \
X(OTA_DOWNLOADING,  "Downloading %d%%",          "下载中 %d%%",           "下載中 %d%%",           "ダウンロード中 %d%%") \
X(OTA_DOWNLOADED,   "Firmware downloaded. Ready to install.", "固件已下载，可以安装。", "韌體已下載，可以安裝。", "ダウンロード完了。インストールできます。") \
X(OTA_INSTALL,      "Install",                   "安装",                  "安裝",                  "インストール") \
X(OTA_INSTALL_NOTE, "Install will reboot to the updater and flash the app. Keep power stable.", "安装将重启到升级器并刷写固件，请保持供电稳定。", "安裝將重新啟動到升級器並燒錄韌體，請保持供電穩定。", "インストールするとアップデーターに再起動して書き込みます。電源を安定させてください。") \
X(OTA_LAUNCHER_MODE,"Booted by Launcher: install via Launcher.", "由 Launcher 启动：请通过 Launcher 更新。", "由 Launcher 啟動：請透過 Launcher 更新。", "Launcher から起動中: Launcher で更新してください。") \
X(OTA_VERIFY_FAILED,"Package verification failed", "升级包校验失败",     "升級包校驗失敗",        "パッケージの検証に失敗") \
X(OTA_HTTP_FAILED,  "HTTP request failed",       "HTTP 请求失败",         "HTTP 請求失敗",         "HTTP リクエストに失敗") \
X(OTA_UPDATER,      "Updater (UPLOAD) firmware", "升级器 (UPLOAD) 固件",  "升級器 (UPLOAD) 韌體",  "アップデーター (UPLOAD)") \
X(OTA_UPDATE_UPDATER, "Update Updater",          "更新升级器",            "更新升級器",            "アップデーターを更新") \
X(OTA_UPDATER_DONE, "Updater firmware updated.", "升级器固件已更新。",    "升級器韌體已更新。",    "アップデーターを更新しました。") \
X(OTA_SD_PACKAGE,   "Update package found on SD. Ready to install.", "SD 卡中发现升级包，可以安装。", "SD 卡中發現升級包，可以安裝。", "SD に更新パッケージがあります。インストールできます。") \
X(OTA_URL,          "Update URL",                "更新地址",              "更新網址",              "更新 URL") \
X(DEVINFO,          "Device Info",               "设备信息",              "裝置資訊",              "デバイス情報") \
X(DEV_CHIP,         "Chip",                      "芯片",                  "晶片",                  "チップ") \
X(DEV_IDF,          "IDF version",               "IDF 版本",              "IDF 版本",              "IDF バージョン") \
X(DEV_APP_VER,      "App version",               "固件版本",              "韌體版本",              "アプリバージョン") \
X(DEV_UPTIME,       "Uptime",                    "运行时间",              "運行時間",              "稼働時間") \
X(DEV_HEAP,         "DRAM free / total",         "DRAM 可用 / 总计",      "DRAM 可用 / 總計",      "DRAM 空き / 合計") \
X(DEV_DMA,          "DMA free / total",          "DMA 可用 / 总计",       "DMA 可用 / 總計",       "DMA 空き / 合計") \
X(DEV_PSRAM,        "PSRAM free / total",        "PSRAM 可用 / 总计",     "PSRAM 可用 / 總計",     "PSRAM 空き / 合計") \
X(DEV_FLASH,        "Flash",                     "Flash",                 "Flash",                 "Flash") \
X(DEV_TF,           "TF card",                   "TF 卡",                 "TF 卡",                 "TF カード") \
X(DEV_BATTERY,      "Battery",                   "电池",                  "電池",                  "バッテリー") \
X(DEV_POWER_SRC,    "Power",                     "供电",                  "供電",                  "電源") \
X(PWR_USB_C,        "USB-C",                     "USB-C 外部供电",        "USB-C 外部供電",        "USB-C 給電") \
X(PWR_USB_C_CHARGE, "USB-C + Battery charging",  "USB-C + 电池充电",      "USB-C + 電池充電",      "USB-C + 充電中") \
X(PWR_USB_C_STANDBY,"USB-C + Battery standby",   "USB-C + 电池待机",      "USB-C + 電池待機",      "USB-C + バッテリー待機") \
X(PWR_BATTERY,      "Battery",                   "电池供电",              "電池供電",              "バッテリー駆動") \
X(DEV_PERIPHERALS,  "Peripherals",               "外设",                  "周邊",                  "周辺機器") \
X(USB_TITLE,        "USB",                       "USB",                   "USB",                   "USB") \
X(USB_DISK_MODE,    "USB Disk Mode",             "USB 磁盘模式",          "USB 磁碟模式",          "USB ディスクモード") \
X(USB_DISK_START,   "Start USB Disk Mode",       "开启 USB 磁盘模式",     "開啟 USB 磁碟模式",     "USB ディスクモードを開始") \
X(USB_DISK_STOP,    "Stop USB Disk Mode",        "关闭 USB 磁盘模式",     "關閉 USB 磁碟模式",     "USB ディスクモードを停止") \
X(USB_DISK_NOTE,    "Connect USB-C to the PC. The PC gets exclusive access to the TF card, and the serial port is unavailable until you stop it.", "请用 USB-C 连接电脑。开启后电脑将独占访问 TF 卡，期间串口不可用。", "請用 USB-C 連接電腦。開啟後電腦將獨占存取 TF 卡，期間序列埠無法使用。", "USB-C を PC に接続してください。有効中は PC が TF カードを占有し、シリアルポートは使えません。") \
X(USB_DISK_ON,      "USB disk mode active: PC has the TF card", "USB 磁盘模式已开启：电脑正在独占访问 TF 卡", "USB 磁碟模式已開啟：電腦正在獨占存取 TF 卡", "USB ディスクモード有効: PC が TF カードを使用中") \
X(USB_DISK_OFF,     "USB disk mode off. TF card restored.", "USB 磁盘模式已关闭，TF 卡已恢复。", "USB 磁碟模式已關閉，TF 卡已恢復。", "USB ディスクモード終了。TF カードを復元しました。") \
X(USB_HOST,         "USB Flash Drive",           "U盘",                   "USB 隨身碟",            "USB メモリ") \
X(USB_MOUNT,        "Mount USB",                 "挂载 U盘",              "掛載 USB 隨身碟",       "USB をマウント") \
X(USB_EJECT,        "Eject USB",                 "弹出 U盘",              "退出 USB 隨身碟",       "USB を取り外す") \
X(USB_MOUNTED,      "USB drive mounted",         "U盘已挂载",             "USB 隨身碟已掛載",      "USB メモリをマウントしました") \
X(USB_NOT_MOUNTED,  "USB drive not mounted",     "U盘未挂载",             "USB 隨身碟未掛載",      "USB メモリが未マウント") \
X(USB_WAITING,      "Waiting for USB device...", "等待 USB 设备...",      "等待 USB 裝置...",      "USB デバイスを待機中...") \
X(USB_FORMAT,       "Format USB (FAT32)",        "格式化 U盘 (FAT32)",    "格式化 USB 隨身碟 (FAT32)", "USB をフォーマット (FAT32)") \
X(USB_FORMAT_WARN,  "Formatting will erase all files on the USB drive.", "格式化将清除 U盘上的所有文件。", "格式化將清除 USB 隨身碟上的所有檔案。", "フォーマットすると USB メモリ内のファイルはすべて消去されます。") \
X(USB_FORMATTED,    "USB drive formatted",       "U盘已格式化",           "USB 隨身碟已格式化",    "USB メモリをフォーマットしました") \
X(STATUS_BAR_TIME,  "Time",                      "时间",                  "時間",                  "時刻") \
X(BATTERY_PCT,      "Battery %d%%",              "电池 %d%%",             "電池 %d%%",             "バッテリー %d%%") \
X(TERM_HINT,        "Edges: top=status, left=controls, right=keyboard", "边缘：上=状态栏，左=控制栏，右=键盘", "邊緣：上=狀態列，左=控制列，右=鍵盤", "画面端: 上=状態、左=操作バー、右=キーボード") \
X(SOFT_KBD,         "Soft Keyboard",             "软键盘",                "軟鍵盤",                "ソフトキーボード") \
X(REBOOT,           "Reboot",                    "重启",                  "重新啟動",              "再起動") \
X(REBOOT_Q,         "Reboot now?",               "现在重启？",            "現在重新啟動？",        "今すぐ再起動しますか？") \
X(ABOUT,            "About",                     "关于",                  "關於",                  "情報") \
X(ABOUT_TEXT,       "Clientre 5: open-source firmware for M5Stack Tab5.\nlibssh (LGPL 2.1), LVGL (MIT), Noto Sans CJK (OFL), SKK-JISYO (GPL).", "Clientre 5：面向 M5Stack Tab5 的开源固件。\nlibssh (LGPL 2.1)、LVGL (MIT)、Noto Sans CJK (OFL)、SKK-JISYO (GPL)。", "Clientre 5：適用於 M5Stack Tab5 的開源韌體。\nlibssh (LGPL 2.1)、LVGL (MIT)、Noto Sans CJK (OFL)、SKK-JISYO (GPL)。", "Clientre 5：M5Stack Tab5 向けオープンソースファームウェア。\nlibssh (LGPL 2.1)、LVGL (MIT)、Noto Sans CJK (OFL)、SKK-JISYO (GPL)。") \
X(SD_LOW_SPACE,     "TF card has less than 20MB free", "TF 卡剩余空间不足 20MB", "TF 卡剩餘空間不足 20MB", "TF カードの空き容量が 20MB 未満です") \
X(TAILSCALE,        "Tailscale",                 "Tailscale",             "Tailscale",             "Tailscale") \
X(TS_ENABLE,        "Join tailnet",              "加入 Tailnet",          "加入 Tailnet",          "Tailnet に参加") \
X(TS_AUTH_KEY,      "Auth key (tskey-auth-...)", "认证密钥 (tskey-auth-...)", "驗證金鑰 (tskey-auth-...)", "認証キー (tskey-auth-...)") \
X(TS_DEVICE_NAME,   "Device name",               "设备名",                "裝置名稱",              "デバイス名") \
X(TS_CTRL_HOST,     "Control server (blank = Tailscale)", "控制服务器 (空 = Tailscale)", "控制伺服器 (空 = Tailscale)", "コントロールサーバー (空 = Tailscale)") \
X(TS_STATUS,        "Status: %s",                "状态：%s",              "狀態：%s",              "状態: %s") \
X(TS_VPN_IP,        "Tailnet IP: %s",            "Tailnet IP：%s",        "Tailnet IP：%s",        "Tailnet IP: %s") \
X(TS_PEERS,         "Peers",                     "节点",                  "節點",                  "ピア") \
X(TS_NO_PEERS,      "No peers yet",              "尚无节点",              "尚無節點",              "ピアはまだありません") \
X(TS_KEY_FROM_SD,   "Load key from SD (/tailscale/authkey.txt)", "从 SD 读取密钥 (/tailscale/authkey.txt)", "從 SD 讀取金鑰 (/tailscale/authkey.txt)", "SD から鍵を読み込む (/tailscale/authkey.txt)") \
X(TS_KEY_LOADED,    "Auth key loaded",           "密钥已读取",            "金鑰已讀取",            "認証キーを読み込みました") \
X(TS_KEY_MISSING,   "No auth key. Create one at login.tailscale.com/admin/settings/keys", "没有认证密钥，请在 login.tailscale.com/admin/settings/keys 创建", "沒有驗證金鑰，請在 login.tailscale.com/admin/settings/keys 建立", "認証キーがありません。login.tailscale.com/admin/settings/keys で作成してください") \
X(TS_DIRECT,        "Direct connections (experimental)", "直接连接 (实验性)",      "直接連線 (實驗性)",      "直接接続 (実験的)") \
X(TS_DIRECT_NOTE,   "Off = always relay via DERP (reliable). On = try direct UDP paths; may stall behind strict NAT.", "关：始终经 DERP 中继（稳定）。开：尝试直连 UDP，严格 NAT 下可能卡住。", "關：始終經 DERP 中繼（穩定）。開：嘗試直連 UDP，嚴格 NAT 下可能卡住。", "オフ: 常に DERP 中継（確実）。オン: 直接 UDP 経路を試す。厳しい NAT 下では止まることがあります。") \
X(TS_FORGET,        "Forget this node",          "忘记此节点",            "忘記此節點",            "このノードを削除") \
X(TS_FORGET_Q,      "Remove node keys? You will need a new auth key.", "删除节点密钥？需要新的认证密钥。", "刪除節點金鑰？需要新的驗證金鑰。", "ノード鍵を削除しますか？新しい認証キーが必要になります。") \
X(TS_ADD_SERVER,    "Add as SSH server",         "添加为 SSH 服务器",     "新增為 SSH 伺服器",     "SSH サーバーとして追加") \
X(TS_NOTE,          "Unofficial client (MicroLink). Hostnames are resolved from the peer list; connections may relay via DERP.", "非官方客户端 (MicroLink)。主机名从节点列表解析，连接可能经 DERP 中继。", "非官方客戶端 (MicroLink)。主機名從節點列表解析，連線可能經 DERP 中繼。", "非公式クライアント (MicroLink) です。ホスト名はピア一覧から解決し、接続は DERP 経由になることがあります。") \
X(PUBLIC_IP,        "Public IP",                 "公网 IP",               "公網 IP",               "グローバル IP") \
X(QUERYING,         "querying...",               "查询中...",             "查詢中...",             "問い合わせ中...")

typedef enum {
#define X(id, en, cn, tw, ja) STR_##id,
    STR_TABLE(X)
#undef X
    STR_COUNT
} str_id_t;

void i18n_set_lang(lang_t lang);
lang_t i18n_get_lang(void);
const char *i18n_lang_name(lang_t lang);
const char *tr(str_id_t id);
/* Translation for a language other than the current one. */
const char *tr_lang(lang_t lang, str_id_t id);

#ifdef __cplusplus
}
#endif
