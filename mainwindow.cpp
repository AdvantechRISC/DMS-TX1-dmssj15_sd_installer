#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "multiimagewritethread.h"
#include "progressslideshowdialog.h"
#include "config.h"
#include "languagedialog.h"
#include "json.h"
#include "util.h"
#include "twoiconsdelegate.h"
#include <QMessageBox>
#include <QProgressDialog>
#include <QMap>
#include <QProcess>
#include <QDir>
#include <QDebug>
#include <QTimer>
#include <QLabel>
#include <QPixmap>
#include <QPainter>
#include <QKeyEvent>
#include <QApplication>
#include <QScreen>
#include <QSplashScreen>
#include <QDesktopWidget>
#include <QSettings>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkDiskCache>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <inttypes.h>

#ifdef Q_WS_QWS
#include <QWSServer>
#endif

/* Main window
 *
 * Initial author: Floris Bos
 * Maintained by Raspberry Pi
 *
 * See LICENSE.txt for license details
 *
 */

/* To keep track of where the different OSes get 'installed' from */
#define SOURCE_SDCARD "sdcard"
#define SOURCE_NETWORK "network"
#define SOURCE_INSTALLED_OS "installed_os"

/* Flag to keep track wheter or not we already repartitioned. */
bool MainWindow::_partInited = false;

/* Flag to keep track of current display mode. */
int MainWindow::_currentMode = 0;

MainWindow::MainWindow(const QString &defaultDisplay, const QString &defaultRoot, const QString &url, QSplashScreen *splash, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    _qpd(NULL), _kcpos(0), _defaultDisplay(defaultDisplay), _root(defaultRoot), repoUrl(url),
    _silent(false), _allowSilent(false), _splash(splash), _settings(NULL),
    _activatedEth(false), _numInstalledOS(0), _netaccess(NULL), _displayModeBox(NULL)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    resize(QDesktopWidget().availableGeometry(this).size() * 0.7);
    setContextMenuPolicy(Qt::NoContextMenu);
    update_window_title();
    _kc << 0x01000013 << 0x01000013 << 0x01000015 << 0x01000015 << 0x01000012
        << 0x01000014 << 0x01000012 << 0x01000014 << 0x42 << 0x41;
    ui->list->setItemDelegate(new TwoIconsDelegate(this));
    ui->list->installEventFilter(this);
    ui->advToolBar->setVisible(false);

    qDebug() << "Root folder is " << _root ;
    qDebug() << "URL is is " << repoUrl ;

    QRect s = QApplication::desktop()->screenGeometry();
    if (s.height() < 500)
        resize(s.width()-10, s.height()-100);

    if (getFileContents("/proc/cmdline").contains("silentinstall"))
    {
        /* If silentinstall is specified, auto-install single image in /os */
        _allowSilent = true;
    }
    else
    {
        startNetworking();
    }

    /* Disable online help buttons until network is functional */
    ui->actionBrowser->setEnabled(false);
    QTimer::singleShot(1, this, SLOT(populate()));
}

MainWindow::~MainWindow()
{
    // QProcess::execute("umount /mnt");
    delete ui;
}

/* Mount FAT partition, discover which images we have, and fill in the list */
void MainWindow::populate()
{
    /* Ask user to wait while list is populated */
    if (!_allowSilent)
    {
        _qpd = new QProgressDialog(tr("Please wait, initialization in progress"), QString(), 0, 0, this);
        _qpd->setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
        _qpd->show();
        QTimer::singleShot(2000, this, SLOT(hideDialogIfNoNetwork()));
    }

    // Fill in list of images
    repopulate();
    updateNeeded();

    if (ui->list->count() != 0)
    {
        ui->list->setCurrentRow(0);
        ui->actionWrite_image_to_disk->setEnabled(true);
    }

    ui->actionCancel->setEnabled(true);
}

void MainWindow::remountSettingsRW()
{
    // QProcess::execute("mount -o remount,rw /settings");
}

void MainWindow::repopulate()
{
    QMap<QString,QVariantMap> images = listImages();
    ui->list->clear();
    ui->list->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->list->setWordWrap(true);
    bool haveicons = false;
    QSize currentsize = ui->list->iconSize();
    QIcon localIcon(":/icons/hdd.png");
    QIcon internetIcon(":/icons/download.png");
    _numInstalledOS = 0;

    foreach (QVariant v, images.values())
    {
        QVariantMap m = v.toMap();
        QString flavour = m.value("name").toString();
        QString description = m.value("description").toString();
        QString folder  = m.value("folder").toString();
        QString iconFilename = m.value("icon").toString();
        bool installed = m.value("installed").toBool();

        if (!iconFilename.isEmpty() && !iconFilename.contains('/'))
            iconFilename = folder+"/"+iconFilename;
        if (!QFile::exists(iconFilename))
        {
            iconFilename = folder+"/"+flavour+".png";
            iconFilename.replace(' ', '_');
        }

        QString friendlyname = flavour;
        if (installed)
        {
            friendlyname += " ["+tr("INSTALLED")+"]";
            _numInstalledOS++;
        }
        if (!description.isEmpty())
            friendlyname += "\n"+description;

        QIcon icon;
        if (QFile::exists(iconFilename))
        {
            icon = QIcon(iconFilename);
            QList<QSize> avs = icon.availableSizes();
            if (avs.isEmpty())
            {
                /* Icon file corrupt */
                icon = QIcon();
            }
            else
            {
                QSize iconsize = avs.first();
                haveicons = true;

                if (iconsize.width() > currentsize.width() || iconsize.height() > currentsize.height())
                {
                    /* Make all icons as large as the largest icon we have */
                    currentsize = QSize(qMax(iconsize.width(), currentsize.width()),qMax(iconsize.height(), currentsize.height()));
                    ui->list->setIconSize(currentsize);
                }
            }
        }
        QListWidgetItem *item = new QListWidgetItem(icon, friendlyname);
        item->setData(Qt::UserRole, m);
        item->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
        if (installed)
        {
            item->setData(Qt::BackgroundColorRole, INSTALLED_OS_BACKGROUND_COLOR);
            //            item->setCheckState(Qt::Checked);
        }
        // else
            // item->setCheckState(Qt::Unchecked);

        if (m["source"] == SOURCE_INSTALLED_OS)
        {
            item->setData(SecondIconRole, QIcon());
        }
        else
        {
            if (folder.startsWith(_root))
                item->setData(SecondIconRole, localIcon);
            else
                item->setData(SecondIconRole, internetIcon);
        }

        ui->list->addItem(item);
    }

    if (haveicons)
    {
        /* Giving items without icon a dummy icon to make them have equal height and text alignment */
        QPixmap dummyicon = QPixmap(currentsize.width(), currentsize.height());
        dummyicon.fill();

        for (int i=0; i< ui->list->count(); i++)
        {
            if (ui->list->item(i)->icon().isNull())
            {
                ui->list->item(i)->setIcon(dummyicon);
            }
        }
    }
}

QMap<QString, QVariantMap> MainWindow::listImages()
{
    QMap<QString,QVariantMap> images;

    /* Local image folders */
    QDir dir(_root, "", QDir::Name, QDir::Dirs | QDir::NoDotAndDotDot);
    QStringList list = dir.entryList();

    foreach (QString image,list)
    {
        QString imagefolder = _root + "/" +image;
        if (!QFile::exists(imagefolder+"/os.json"))
            continue;

        qDebug() << "New Image" << image ;

        QVariantMap osv = Json::loadFromFile(imagefolder+"/os.json").toMap();
        osv["source"] = SOURCE_SDCARD;

        QString basename = osv.value("name").toString();

        qDebug() << "New Image" << basename ;

        if (QFile::exists(imagefolder+"/flavours.json"))
          {
            QVariantMap v = Json::loadFromFile(imagefolder+"/flavours.json").toMap();
            QVariantList fl = v.value("flavours").toList();

            foreach (QVariant f, fl)
              {
                QVariantMap fm  = f.toMap();
                if (fm.contains("name"))
                  {
                    QString name = fm.value("name").toString();
                    fm["folder"] = imagefolder;
                    fm["release_date"] = osv.value("release_date");
                    fm["source"] = osv.value("source");
                    images[name] = fm;
                  }
              }
          }
        else
          {
            QString name = basename;
            osv["folder"] = imagefolder;
            images[name] = osv;
          }
        qDebug() << "New Image, can install" << image ;

    }

    return images;
}

void MainWindow::on_actionAbout_triggered()
{
  QRect s = QApplication::desktop()->screenGeometry();
  QRect t = geometry();
  QMessageBox::about(this,
                     tr("About"),
                     QString(tr("Built: %1\n")).arg( QString::fromLocal8Bit(__DATE__)) +
                     QString(tr("Version: git:%1\n")).arg(GIT_VERSION) +
                     QString(tr("Display resolution: %1 x %2\n")).arg(s.width()).arg(s.height())+
                     QString(tr("Window resolution: %1 x %2\n")).arg(t.width()).arg(t.height())
                     );
}

void MainWindow::on_actionWrite_image_to_disk_triggered()
{

    if (_silent || QMessageBox::warning(this,
                                        tr("Confirm"),
                                        tr("Warning: this will install the selected Operating System. All existing data on the eMMC will be overwritten, including any OSes that are already installed."),
                                        QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes)
    {
      setEnabled(false);
      _numMetaFilesToDownload = 0;

      QList<QListWidgetItem *> selected = selectedItems();
      foreach (QListWidgetItem *item, selected)
        {
          QVariantMap entry = item->data(Qt::UserRole).toMap();

          if (!entry.contains("folder"))
            {
              QDir d;
              QString folder = "/settings/os/"+entry.value("name").toString();
              folder.replace(' ', '_');
              if (!d.exists(folder))
                d.mkpath(folder);

              downloadMetaFile(entry.value("os_info").toString(), folder+"/os.json");
              downloadMetaFile(entry.value("partitions_info").toString(), folder+"/partitions.json");

              if (entry.contains("marketing_info"))
                downloadMetaFile(entry.value("marketing_info").toString(), folder+"/marketing.tar");

              if (entry.contains("partition_setup"))
                downloadMetaFile(entry.value("partition_setup").toString(), folder+"/partition_setup.sh");

              if (entry.contains("icon"))
                downloadMetaFile(entry.value("icon").toString(), folder+"/icon.png");
            }
        }

      if (_numMetaFilesToDownload == 0)
        {
          /* All OSes selected are local */
          startImageWrite();
        }
      else if (!_silent)
        {
          _qpd = new QProgressDialog(tr("The install process will begin shortly."), QString(), 0, 0, this);
          _qpd->setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
          _qpd->show();
        }
    }
}

void MainWindow::on_actionCancel_triggered()
{
    close();
}

void MainWindow::onCompleted()
{
    _qpd->hide();
    // QProcess::execute("mount -o remount,rw /settings");
    // QSettings settings("/settings/noobs.conf", QSettings::IniFormat, this);
    // settings.setValue("default_partition_to_boot", "800");
    // settings.sync();
    // QProcess::execute("mount -o remount,ro /settings");

    if (!_silent)
      QMessageBox::information(this,
                               tr("OS installed"),
                               tr("Flashing has completed, and OS is installed successfully.\n") +
                               tr("Please remove SD Card, when you are ready, press OK to reboot"),
                               QMessageBox::Ok);

    _qpd->deleteLater();
    _qpd = NULL;
    close();
}

void MainWindow::onError(const QString &msg)
{
    _qpd->hide();
    QMessageBox::critical(this, tr("Error"), msg, QMessageBox::Close);
    setEnabled(true);
}

void MainWindow::onQuery(const QString &msg, const QString &title, QMessageBox::StandardButton* answer)
{
    _qpd->hide();
    *answer = QMessageBox::question(this, title, msg, QMessageBox::Yes|QMessageBox::No);
    setEnabled(true);
}

void MainWindow::on_list_currentRowChanged()
{
}

void MainWindow::update_window_title()
{
    setWindowTitle(QString(tr("Advantech Installer for DMS-SJ15")));
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event && event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
        update_window_title();
        updateNeeded();
        //repopulate();
    }

    QMainWindow::changeEvent(event);
}

void MainWindow::displayMode(int modenr, bool silent)
{
#ifdef Q_WS_QWS
    QString cmd, mode;

    if (!silent && _displayModeBox)
    {
        /* User pressed another mode selection key while the confirmation box is being displayed */
        silent = true;
        _displayModeBox->close();
    }

    switch (modenr)
    {
    case 0:
        cmd  = "-p";
        mode = tr("HDMI preferred mode");
        break;
    case 1:
        cmd  = "-e \'DMT 4 DVI\'";
        mode = tr("HDMI safe mode");
        break;
    case 2:
        cmd  = "-c \'PAL 4:3\'";
        mode = tr("composite PAL mode");
        break;
    case 3:
        cmd  = "-c \'NTSC 4:3\'";
        mode = tr("composite NTSC mode");
        break;

    default:
        // unknown mode
        return;
    }
    _currentMode = modenr;

    // Trigger framebuffer resize
    QProcess *presize = new QProcess(this);
    presize->start(QString("sh -c \"tvservice -o; tvservice %1;\"").arg(cmd));
    presize->waitForFinished(4000);

    // Update screen resolution with current value (even if we didn't
    // get what we thought we'd get)
    QProcess *update = new QProcess(this);
    update->start(QString("sh -c \"tvservice -s | cut -d , -f 2 | cut -d \' \' -f 2 | cut -d x -f 1;tvservice -s | cut -d , -f 2 | cut -d \' \' -f 2 | cut -d x -f 2\""));
    update->waitForFinished(4000);
    update->setProcessChannelMode(QProcess::MergedChannels);

    QTextStream stream(update);
    int xres = stream.readLine().toInt();
    int yres = stream.readLine().toInt();
    int oTop = 0, oBottom = 0, oLeft = 0, oRight = 0;
    getOverscan(oTop, oBottom, oLeft, oRight);
    qDebug() << "Current overscan" << "top" << oTop << "bottom" << oBottom << "left" << oLeft << "right" << oRight;
    QScreen::instance()->setMode(xres-oLeft-oRight, yres-oTop-oBottom, 16);

    // Resize this window depending on screen resolution
    QRect s = QApplication::desktop()->screenGeometry();
    if (s.height() < 500)
        resize(s.width()-10, s.height()-100);
    else
        resize(575, 450);

    // Update UI item locations
    _splash->setPixmap(QPixmap(":/wallpaper.png"));
    LanguageDialog *ld = LanguageDialog::instance("en", "gb");
    ld->setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignHCenter | Qt::AlignBottom, ld->size(), qApp->desktop()->availableGeometry()));
    this->setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, this->size(), qApp->desktop()->availableGeometry()));

    // Refresh screen
    qApp->processEvents();
    QWSServer::instance()->refresh();

    // In case they can't see the message box, inform that mode change
    // is occuring by turning on the LED during the change
    QProcess *led_blink = new QProcess(this);
    connect(led_blink, SIGNAL(finished(int)), led_blink, SLOT(deleteLater()));
    led_blink->start("sh -c \"echo 1 > /sys/class/leds/led0/brightness; sleep 3; echo 0 > /sys/class/leds/led0/brightness\"");

    // Inform user of resolution change with message box.
    if (!silent && _settings)
    {
        _displayModeBox = new QMessageBox(QMessageBox::Question,
                      tr("Display Mode Changed"),
                      tr("Display mode changed to %1\nWould you like to make this setting permanent?").arg(mode),
                      QMessageBox::Yes | QMessageBox::No);
        _displayModeBox->installEventFilter(this);
        _displayModeBox->exec();

        if (_displayModeBox->standardButton(_displayModeBox->clickedButton()) == QMessageBox::Yes)
        {
            remountSettingsRW();
            _settings->setValue("display_mode", modenr);
            _settings->sync();
            ::sync();
        }
        _displayModeBox = NULL;
    }

    /*
    QMessageBox *mbox = new QMessageBox;
    mbox->setWindowTitle(tr("Display Mode Changed"));
    mbox->setText(QString(tr("Display mode changed to %1")).arg(mode));
    mbox->setStandardButtons(0);
    mbox->show();
    QTimer::singleShot(2000, mbox, SLOT(hide()));
    */

#else
    Q_UNUSED(modenr)
    Q_UNUSED(silent)
#endif
}

bool MainWindow::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);

        // Let user find the best display mode for their display
        // experimentally by using keys 1-4. NOOBS will default to using HDMI preferred mode.

        // HDMI preferred mode
        if (keyEvent->key() == Qt::Key_1 && _currentMode != 0)
        {
            displayMode(0);
        }
        // HDMI safe mode
        if (keyEvent->key() == Qt::Key_2 && _currentMode != 1)
        {
            displayMode(1);
        }
        // Composite PAL
        if (keyEvent->key() == Qt::Key_3 && _currentMode != 2)
        {
            displayMode(2);
        }
         // Composite NTSC
        if (keyEvent->key() == Qt::Key_4 && _currentMode != 3)
        {
            displayMode(3);
        }
        // Catch Return key to trigger OS boot
        if (keyEvent->key() == Qt::Key_Return)
        {
        }
        else if (_kc.at(_kcpos) == keyEvent->key())
        {
            _kcpos++;
            if (_kcpos == _kc.size())
            {
                inputSequence();
                _kcpos = 0;
            }
        }
        else
            _kcpos=0;
    }

    return false;
}

void MainWindow::inputSequence()
{
    QLabel* info = new QLabel(this);
    info->setPixmap(QPixmap("/usr/data"));
    info->setGeometry(0,0,640,480);
    info->show();
}

void MainWindow::on_actionAdvanced_triggered(bool checked)
{
    ui->advToolBar->setVisible(checked);
}


void MainWindow::on_actionBrowser_triggered()
{
    startBrowser();
}

bool MainWindow::requireNetwork()
{
    if (!QFile::exists("/tmp/resolv.conf"))
    {
        QMessageBox::critical(this,
                              tr("No network access"),
                              tr("Wired network access is required for this feature. Please insert a network cable into the network port."),
                              QMessageBox::Close);
        return false;
    }

    return true;
}

void MainWindow::startBrowser()
{
    if (!requireNetwork())
        return;
    QProcess *proc = new QProcess(this);
    QString lang = LanguageDialog::instance("en", "gb")->currentLanguage();
    if (lang == "gb" || lang == "us" || lang == "")
        lang = "en";
    proc->start("arora -lang "+lang+" "+HOMEPAGE);
}


void MainWindow::startNetworking()
{
#if 1
    if (!QFile::exists("/sys/class/net/eth0"))
    {
        /* eth0 not available yet, check back in a tenth of a second */
        QTimer::singleShot(100, this, SLOT(startNetworking()));
        return;
    }

    QByteArray carrier = getFileContents("/sys/class/net/eth0/carrier").trimmed();
    if (carrier.isEmpty() && !_activatedEth)
    {
        QProcess::execute("/sbin/ifconfig eth0 up");
        _activatedEth = true;
    }

    if (carrier != "1")
    {
        /* cable not detected yet, check back in a tenth of a second */
        QTimer::singleShot(100, this, SLOT(startNetworking()));
        return;
    }

    QProcess *proc = new QProcess(this);
    connect(proc, SIGNAL(finished(int)), this, SLOT(ifupFinished(int)));
    /* Try enabling interface twice as sometimes it times out before getting a DHCP lease */
    proc->start("sh -c \"ifup eth0 || ifup eth0\"");
#endif
}

void MainWindow::ifupFinished(int)
{
    QProcess *p = qobject_cast<QProcess*> (sender());

    if (QFile::exists("/etc/resolv.conf"))
    {
        qDebug() << "Network up";
        if (!_netaccess)
        {
            remountSettingsRW();
            QDir dir;
            dir.mkdir("/settings/cache");
            _netaccess = new QNetworkAccessManager(this);
            QNetworkDiskCache *_cache = new QNetworkDiskCache(this);
            _cache->setCacheDirectory("/settings/cache");
            _cache->setMaximumCacheSize(8 * 1024 * 1024);
            _netaccess->setCache(_cache);

            downloadList(repoUrl);
        }
        ui->actionBrowser->setEnabled(true);
        emit networkUp();
    }

    p->deleteLater();
}

void MainWindow::downloadList(const QString &urlstring)
{
    QNetworkReply *reply = _netaccess->get(QNetworkRequest(QUrl(urlstring)));
    connect(reply, SIGNAL(finished()), this, SLOT(downloadListRedirectCheck()));
}


void MainWindow::downloadListComplete()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    int httpstatuscode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != reply->NoError || httpstatuscode < 200 || httpstatuscode > 399)
    {
        _qpd->hide();
        QMessageBox::critical(this, tr("Download error"), tr("Error downloading distribution list from Internet"), QMessageBox::Close);
    }
    else
    {
        processJson(Json::parse( reply->readAll() ));
    }

    reply->deleteLater();
}

void MainWindow::processJson(QVariant json)
{
    if (json.isNull())
    {
        QMessageBox::critical(this, tr("Error"), tr("Error parsing list.json downloaded from server"), QMessageBox::Close);
        return;
    }

    QSet<QString> iconurls;
    QVariantList list = json.toMap().value("os_list").toList();

    foreach (QVariant osv, list)
    {
        QVariantMap  os = osv.toMap();

        QString basename = os.value("os_name").toString();

        if (os.contains("flavours"))
          {
            QVariantList flavours = os.value("flavours").toList();

            foreach (QVariant flv, flavours)
              {
                QVariantMap flavour = flv.toMap();
                QVariantMap item = os;
                QString name        = flavour.value("name").toString();
                QString description = flavour.value("description").toString();
                QString iconurl     = flavour.value("icon").toString();

                item.insert("name", name);
                item.insert("description", description);
                item.insert("icon", iconurl);
                item.insert("feature_level", flavour.value("feature_level"));
                item.insert("source", SOURCE_NETWORK);

                processJsonOs(name, item, iconurls);
              }
          }

        if (os.contains("description"))
          {
            QString name = basename;
            os["name"] = name;
            os["source"] = SOURCE_NETWORK;
            processJsonOs(name, os, iconurls);
          }
    }

    /* Download icons */
    _numIconsToDownload = iconurls.count();

    if (_numIconsToDownload)
    {
        foreach (QString iconurl, iconurls)
        {
            downloadIcon(iconurl, iconurl);
        }
    }
    else
    {
        if (_qpd)
        {
            _qpd->deleteLater();
            _qpd = NULL;
        }
    }
}

void MainWindow::processJsonOs(const QString &name, QVariantMap &new_details, QSet<QString> &iconurls)
{
    QIcon internetIcon(":/icons/download.png");

    QListWidgetItem *witem = findItem(name);
    if (witem)
    {
        QVariantMap existing_details = witem->data(Qt::UserRole).toMap();

        if ((existing_details["release_date"].toString() < new_details["release_date"].toString()) || (existing_details["source"].toString() == SOURCE_INSTALLED_OS))
        {
            /* Local version is older (or unavailable). Replace info with newer Internet version */
            new_details.insert("installed", existing_details.value("installed", false));
            if (existing_details.contains("partitions"))
            {
                new_details["partitions"] = existing_details["partitions"];
            }
            witem->setData(Qt::UserRole, new_details);
            witem->setData(SecondIconRole, internetIcon);
            ui->list->update();
        }

    }
    else
    {
        /* It's a new OS, so add it to the list */
        QString iconurl = new_details.value("icon").toString();
        QString description = new_details.value("description").toString();

        if (!iconurl.isEmpty())
            iconurls.insert(iconurl);

        QString friendlyname = name;
        if (!description.isEmpty())
            friendlyname += "\n"+description;

        witem = new QListWidgetItem(friendlyname);
        witem->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);
        // witem->setCheckState(Qt::Unchecked);
        witem->setData(Qt::UserRole, new_details);
        witem->setData(SecondIconRole, internetIcon);

        ui->list->addItem(witem);
    }
}

void MainWindow::downloadIcon(const QString &urlstring, const QString &originalurl)
{
    QUrl url(urlstring);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::User, originalurl);
    QNetworkReply *reply = _netaccess->get(request);
    connect(reply, SIGNAL(finished()), this, SLOT(downloadIconRedirectCheck()));
}

QListWidgetItem *MainWindow::findItem(const QVariant &name)
{
    for (int i=0; i<ui->list->count(); i++)
    {
        QListWidgetItem *item = ui->list->item(i);
        QVariantMap m = item->data(Qt::UserRole).toMap();
        if (m.value("name").toString() == name.toString())
        {
            return item;
        }
    }
    return NULL;
}

void MainWindow::downloadIconComplete()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    QString url = reply->url().toString();
    QString originalurl = reply->request().attribute(QNetworkRequest::User).toString();
    int httpstatuscode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != reply->NoError || httpstatuscode < 200 || httpstatuscode > 399)
    {
        //QMessageBox::critical(this, tr("Download error"), tr("Error downloading icon '%1'").arg(reply->url().toString()), QMessageBox::Close);
        qDebug() << "Error downloading icon" << url;
    }
    else
    {
        QPixmap pix;
        pix.loadFromData(reply->readAll());
        QIcon icon(pix);

        for (int i=0; i<ui->list->count(); i++)
        {
            QVariantMap m = ui->list->item(i)->data(Qt::UserRole).toMap();
            ui->list->setIconSize(QSize(40,40));
            if (m.value("icon") == originalurl)
            {
                ui->list->item(i)->setIcon(icon);
            }
        }
    }
    if (--_numIconsToDownload == 0 && _qpd)
    {
        _qpd->hide();
        _qpd->deleteLater();
        _qpd = NULL;
    }

    reply->deleteLater();
}

QList<QListWidgetItem *> MainWindow::selectedItems()
{
  return ui->list->selectedItems();
}

void MainWindow::updateNeeded()
{
    bool enableOk = false;
    _neededMB = 0;
    QList<QListWidgetItem *> selected = selectedItems();

    enableOk = true;

    ui->actionWrite_image_to_disk->setEnabled(enableOk);

}

void MainWindow::on_list_itemChanged(QListWidgetItem *)
{
    updateNeeded();
}

void MainWindow::downloadMetaFile(const QString &urlstring, const QString &saveAs)
{
    qDebug() << "Downloading" << urlstring << "to" << saveAs;
    _numMetaFilesToDownload++;
    QUrl url(urlstring);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::User, saveAs);
    QNetworkReply *reply = _netaccess->get(request);
    connect(reply, SIGNAL(finished()), this, SLOT(downloadMetaRedirectCheck()));
}

void MainWindow::downloadListRedirectCheck()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    int httpstatuscode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString redirectionurl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toString();

    if (httpstatuscode > 300 && httpstatuscode < 400)
    {
        qDebug() << "Redirection - Re-trying download from" << redirectionurl;
        _numMetaFilesToDownload--;
        downloadList(redirectionurl);
    }
    else
        downloadListComplete();
}

void MainWindow::downloadIconRedirectCheck()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    int httpstatuscode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString redirectionurl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toString();
    QString originalurl = reply->request().attribute(QNetworkRequest::User).toString();;

    if (httpstatuscode > 300 && httpstatuscode < 400)
    {
        qDebug() << "Redirection - Re-trying download from" << redirectionurl;
        _numMetaFilesToDownload--;
        downloadIcon(redirectionurl, originalurl);
    }
    else
        downloadIconComplete();
}

void MainWindow::downloadMetaRedirectCheck()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    int httpstatuscode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString redirectionurl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toString();
    QString saveAs = reply->request().attribute(QNetworkRequest::User).toString();

    if (httpstatuscode > 300 && httpstatuscode < 400)
    {
        qDebug() << "Redirection - Re-trying download from" << redirectionurl;
        _numMetaFilesToDownload--;
        downloadMetaFile(redirectionurl, saveAs);
    }
    else
        downloadMetaComplete();
}

void MainWindow::downloadMetaComplete()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    int httpstatuscode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != reply->NoError || httpstatuscode < 200 || httpstatuscode > 399)
    {
        if (_qpd)
        {
            _qpd->hide();
            _qpd->deleteLater();
            _qpd = NULL;
        }
        QMessageBox::critical(this, tr("Download error"), tr("Error downloading meta file")+"\n"+reply->url().toString(), QMessageBox::Close);
        setEnabled(true);
    }
    else
    {
        QString saveAs = reply->request().attribute(QNetworkRequest::User).toString();
        QFile f(saveAs);
        f.open(f.WriteOnly);
        if (f.write(reply->readAll()) == -1)
        {
            QMessageBox::critical(this, tr("Download error"), tr("Error writing downloaded file to SD card. SD card or file system may be damaged."), QMessageBox::Close);
            setEnabled(true);
        }
        else
        {
            _numMetaFilesToDownload--;
        }
        f.close();
    }

    if (_numMetaFilesToDownload == 0)
    {
        if (_qpd)
        {
            _qpd->hide();
            _qpd->deleteLater();
            _qpd = NULL;
        }
        startImageWrite();
    }
}

void MainWindow::startImageWrite()
{
    /* All meta files downloaded, extract slides tarball, and launch image writer thread */
    MultiImageWriteThread *imageWriteThread = new MultiImageWriteThread(_root);
    QString folder, slidesFolder;
    QStringList slidesFolders;

    QList<QListWidgetItem *> selected = selectedItems();
    foreach (QListWidgetItem *item, selected)
    {
        QVariantMap entry = item->data(Qt::UserRole).toMap();

        if (entry.contains("folder"))
        {
            /* Local image */
            folder = entry.value("folder").toString();
        }
        else
        {
            folder = "/settings/os/"+entry.value("name").toString();
            folder.replace(' ', '_');

            QString marketingTar = folder+"/marketing.tar";
            if (QFile::exists(marketingTar))
            {
                /* Extract tarball with slides */
                QProcess::execute("tar xf "+marketingTar+" -C "+folder);
                QFile::remove(marketingTar);
            }

            /* Insert tarball download URL information into partition_info.json */
            QVariantMap json = Json::loadFromFile(folder+"/partitions.json").toMap();
            QVariantList partitions = json["partitions"].toList();
            int i=0;
            QStringList tarballs = entry.value("tarballs").toStringList();
            foreach (QString tarball, tarballs)
            {
                QVariantMap partition = partitions[i].toMap();
                partition.insert("tarball", tarball);
                partitions[i] = partition;
                i++;
            }
            json["partitions"] = partitions;
            Json::saveToFile(folder+"/partitions.json", json);
        }

        slidesFolder.clear();
        //QRect s = QApplication::desktop()->screenGeometry();
        //if (s.width() > 640 && QFile::exists(folder+"/slides"))
        //{
        //    slidesFolder = folder+"/slides";
        //}
        if (QFile::exists(folder+"/slides_vga"))
        {
            slidesFolder = folder+"/slides_vga";
        }
        imageWriteThread->addImage(folder, entry.value("name").toString());
        if (!slidesFolder.isEmpty())
            slidesFolders.append(slidesFolder);
    }

    if (slidesFolders.isEmpty())
        slidesFolders.append(_root + "/defaults/slides");

    _qpd = new ProgressSlideshowDialog(slidesFolders, "", 20, this);
    connect(imageWriteThread, SIGNAL(parsedImagesize(qint64)), _qpd, SLOT(setMaximum(qint64)));
    connect(imageWriteThread, SIGNAL(completed()), this, SLOT(onCompleted()));
    connect(imageWriteThread, SIGNAL(error(QString)), this, SLOT(onError(QString)));
    connect(imageWriteThread, SIGNAL(statusUpdate(QString)), _qpd, SLOT(setLabelText(QString)));
    connect(imageWriteThread, SIGNAL(runningMKFS()), _qpd, SLOT(pauseIOaccounting()), Qt::BlockingQueuedConnection);
    connect(imageWriteThread, SIGNAL(finishedMKFS()), _qpd , SLOT(resumeIOaccounting()), Qt::BlockingQueuedConnection);
    imageWriteThread->start();
    hide();
    _qpd->exec();
}

void MainWindow::hideDialogIfNoNetwork()
{
    if (_qpd)
    {
        QByteArray carrier = getFileContents("/sys/class/net/eth0/carrier").trimmed();
        if (carrier != "1")
        {
            /* No network cable inserted */
            _qpd->hide();
            _qpd->deleteLater();
            _qpd = NULL;

            if (ui->list->count() == 0)
            {
                /* No local images either */
                QMessageBox::critical(this,
                                      tr("No network access"),
                                      tr("Network access is required to use without local images."),
                                      QMessageBox::Close);
            }
        }
    }
}
