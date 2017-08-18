#include "mainwindow.h"
#include "languagedialog.h"
#include "config.h"
#include "keydetection.h"
#include "json.h"
#include "util.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <linux/reboot.h>
#include <QApplication>
#include <QStyle>
#include <QDesktopWidget>
#include <QSplashScreen>
#include <QFile>
#include <QIcon>
#include <QProcess>
#include <QDir>
#include <QDebug>

#ifdef Q_WS_QWS
#include <QWSServer>
#endif

/*
 *
 * Initial author: Floris Bos
 * Maintained by Raspberry Pi
 *
 * See LICENSE.txt for license details
 *
 */

void reboot_to_extended()
{
    // Unmount any open file systems
    QProcess::execute("umount -r /mnt");

    // Shut down networking
    // QProcess::execute("ifdown -a");

    // Reboot
    ::reboot(RB_AUTOBOOT);
}

int main(int argc, char *argv[])
{
    // Wait for keyboard to appear before displaying anything
    // KeyDetection::waitForKeyboard();

    // int rev = readBoardRevision();

    // qDebug() << "Board revision is " << rev;

    // int gpioChannel;

    // if (rev == 2 || rev == 3)
    //     gpioChannel = 0;
    // else
    //     gpioChannel = 2;

    QApplication a(argc, argv);
    // RightButtonFilter rbf;
    // GpioInput gpio(gpioChannel);

    QString defaultLang = "en";
    QString defaultKeyboard = "gb";
    QString defaultDisplay = "0";
    QString defaultPartition = "800";
    QString defaultRoot = "/mnt";
    QString defaultUrl = DEFAULT_REPO_SERVER;

    // Process command-line arguments
    for (int i=1; i<argc; i++)
    {
        // Allow default language to be specified in commandline
        if (strcmp(argv[i], "-lang") == 0)
        {
            if (argc > i+1)
                defaultLang = argv[i+1];
        }
        // Allow default keyboard layout to be specified in commandline
        else if (strcmp(argv[i], "-kbdlayout") == 0)
        {
            if (argc > i+1)
                defaultKeyboard = argv[i+1];
        }
        // Allow default display mode to be specified in commandline
        else if (strcmp(argv[i], "-dispmode") == 0)
        {
            if (argc > i+1)
                defaultDisplay = --argv[i+1];
        }
        // Allow default boot partition to be specified in commandline
        else if (strcmp(argv[i], "-partition") == 0)
        {
            if (argc > i+1)
                defaultPartition = argv[i+1];
        }
        else if (strcmp(argv[i], "-root") == 0)
        {
            if (argc > i+1)
                defaultRoot = argv[i+1];

        }
        else if (strcmp(argv[i], "-url") == 0)
        {
            if (argc > i+1)
                defaultUrl = argv[i+1];

        }
    }

    // Intercept right mouse clicks sent to the title bar
    //    a.installEventFilter(&rbf);

#ifdef Q_WS_QWS
    QWSServer::setCursorVisible(false);
#endif

    // Set wallpaper and icon, if we have resource files for that
    if (QFile::exists(":/icons/adv_log.png"))
        a.setWindowIcon(QIcon(":/icons/adv_log.png"));

#ifdef Q_WS_QWS
        QWSServer::setBackground(BACKGROUND_COLOR);
#endif
        QSplashScreen *splash = new QSplashScreen(QPixmap(":/icons/adv_log.png"));

        QApplication::processEvents();

#if 0        
    // If -runinstaller is not specified, only continue if SHIFT is pressed, GPIO is triggered,
    // or no OS is installed (/dev/mmcblk0p5 does not exist)
    bool bailout = !runinstaller
        && !force_trigger
        && !(gpio_trigger && (gpio.value() == 0 ))
        && !(keyboard_trigger && KeyDetection::isF10pressed());

    // Default to booting first extended partition
    setRebootPartition("5");

    if (bailout)
    {
        reboot_to_extended(defaultPartition, true);
    }
#endif

#ifdef Q_WS_QWS
    QWSServer::setCursorVisible(true);
#endif

    // Main window in the middle of screen
    MainWindow mw(defaultDisplay, defaultRoot, defaultUrl, splash);
    mw.setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, mw.size(), a.desktop()->availableGeometry()));
    mw.show();

#ifdef ENABLE_LANGUAGE_CHOOSER
     // Language chooser at the bottom center
    LanguageDialog* ld = new LanguageDialog(defaultLang, defaultKeyboard);
    ld->setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignHCenter | Qt::AlignBottom, ld->size(), a.desktop()->availableGeometry()));
    ld->show();
#endif

    a.exec();
    reboot_to_extended();
    return 0;
}
