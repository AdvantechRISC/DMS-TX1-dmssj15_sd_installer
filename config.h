#ifndef CONFIG_H
#define CONFIG_H

/* Color of the background */
// #define BACKGROUND_COLOR  Qt::white
#define BACKGROUND_COLOR  QColor(0xde, 0xde, 0xde)

/* Highlight color of installed OS */
#define INSTALLED_OS_BACKGROUND_COLOR  QColor(0xef,0xff,0xef)

/* Enable language selection */
/* #define ENABLE_LANGUAGE_CHOOSER */

/* Website launched when launching Arora */
#define HOMEPAGE  "http://www.advantech.com.tw"

/* Location to download the list of available distributions from */
#define DEFAULT_REPO_SERVER  "http://10.42.0.1/noobs/os_list_v2.json"

#define SETTINGS_PARTITION  "/dev/mmcblk0p12"

#endif // CONFIG_H
