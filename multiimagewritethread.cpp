#include "multiimagewritethread.h"
#include "config.h"
#include "json.h"
#include "util.h"
#include "mbr.h"
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTime>
#include <unistd.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <usb.h>

MultiImageWriteThread::MultiImageWriteThread(const QString &folder, QObject *parent) :
  QThread(parent), _root(folder)
{
    // QDir dir;

    // if (!dir.exists("/mnt2"))
    //     dir.mkdir("/mnt2");
}

bool MultiImageWriteThread::check_flash_target()
{
    struct usb_bus *bus;
    struct usb_device *dev;
    usb_init();
    usb_find_busses();
    usb_find_devices();
    for (bus = usb_busses; bus; bus = bus->next) {
        for (dev = bus->devices; dev; dev = dev->next){

            if ((dev->descriptor.idVendor == 0x0955) && (dev->descriptor.idProduct == 0x7721))
                return true;
        }
    }

    return false;
}

void MultiImageWriteThread::addImage(const QString &folder, const QString &flavour)
{
    _images.insert(folder, flavour);
    qDebug() << "Add image to write: " << folder << ", " << flavour;
}

void MultiImageWriteThread::run()
{

    /* Process each image */
    for (QMultiMap<QString,QString>::const_iterator iter = _images.constBegin(); iter != _images.constEnd(); iter++)
    {
      qDebug() << "Processing image";
      if (!processImage(iter.key(), iter.value()))
            return;
    }

    emit statusUpdate(tr("Finish writing (sync)"));
    //    sync();
    emit completed();
}

bool MultiImageWriteThread::processImage(const QString &folder, const QString &flavour)
{

    QString os_name = (folder.split("/")).at(2);

    qDebug() << "Processing OS:" << os_name;

    emit statusUpdate(tr("%1: About to flash").arg(os_name));

    QProcess entryTX1Recovery;
    entryTX1Recovery.setWorkingDirectory(folder);
    entryTX1Recovery.start(_root + "/entry_recovery");

    entryTX1Recovery.waitForFinished(-1);

    if (entryTX1Recovery.exitCode() != 0)
    {
        qDebug() << entryTX1Recovery.errorString();
        qDebug() << entryTX1Recovery.readAllStandardError();
        qDebug() << "exit code" << entryTX1Recovery.exitCode();
        qDebug() << "exit status" << entryTX1Recovery.exitStatus();
        emit error(tr("%1: Error when TX1 entry recovery mode").arg(os_name));
        return false;
    }

    usleep(5 * 1000 * 1000);

    if (!check_flash_target()) {
        qDebug() << "Can't to find the flash target!";
        emit error(tr("%1: Error when find the flash target!").arg(os_name));
        return false;
    }

    QProcess burn;
    burn.setWorkingDirectory(folder);
    qDebug() << "Working folder: " << folder;
    qDebug() << "Flavour: " << flavour;
    burn.start(_root + "/flash -o /dev/mmcblk0");
    
    emit statusUpdate(tr("%1: Flashing in progress").arg(os_name));
    
    burn.waitForFinished(-1);

    if (burn.exitCode() != 0)
    {
        qDebug() << burn.errorString();
        qDebug() << burn.readAllStandardError();
        qDebug() << "exit code" << burn.exitCode();
        qDebug() << "exit status" << burn.exitStatus();
        emit error(tr("%1: Error when flashing file system").arg(os_name));
        return false;
    }

    qDebug() << burn.readAllStandardOutput();
    qDebug() << burn.readAllStandardError();
    
    return true;
}

void MultiImageWriteThread::clearEBR()
{
    mbr_table ebr;
    int startOfExtended = getFileContents("/sys/class/block/mmcblk0p2/start").trimmed().toULongLong();

    /* Write out empty extended partition table with signature */
    memset(&ebr, 0, sizeof ebr);
    ebr.signature[0] = 0x55;
    ebr.signature[1] = 0xAA;
    QFile f("/dev/mmcblk0");
    f.open(f.ReadWrite);
    f.seek(qint64(startOfExtended)*512);
    f.write((char *) &ebr, sizeof(ebr));
    f.close();
}

bool MultiImageWriteThread::addPartitionEntry(int sizeInSectors, int type, int specialOffset)
{
    /* Unmount everything before modifying partition table */
    //QProcess::execute("umount -r /mnt");
    //QProcess::execute("umount -r /settings");

    unsigned int startOfExtended = getFileContents("/sys/class/block/mmcblk0p2/start").trimmed().toULongLong();
    unsigned int offsetInSectors = 0;
    mbr_table ebr;
    QFile f("/dev/mmcblk0");
    if (!f.open(f.ReadWrite))
    {
        emit error(tr("Error opening /dev/mmcblk0 for writing"));
        return false;
    }

    /* Find last EBR */
    do
    {
        f.seek(qint64(startOfExtended+offsetInSectors)*512);
        f.read((char *) &ebr, sizeof(ebr));

        if (ebr.part[1].starting_sector)
        {
            if (ebr.part[1].starting_sector > offsetInSectors)
            {
                offsetInSectors = ebr.part[1].starting_sector;
            }
            else
            {
                emit error(tr("Internal error in partitioning"));
                return false;
            }
        }
    } while (ebr.part[1].starting_sector != 0);

    if (ebr.part[0].starting_sector)
    {
        /* Add reference to new EBR to old last EBR */
        ebr.part[1].starting_sector = offsetInSectors+ebr.part[0].starting_sector+ebr.part[0].nr_of_sectors;
        ebr.part[1].nr_of_sectors = sizeInSectors + specialOffset + 2048;
        ebr.part[1].id = 0x0F;

        f.seek(qint64(startOfExtended+offsetInSectors)*512);
        f.write((char *) &ebr, sizeof(ebr));
        offsetInSectors = ebr.part[1].starting_sector;
        qDebug() << "add tail";
    }

    memset(&ebr, 0, sizeof ebr);
    ebr.signature[0] = 0x55;
    ebr.signature[1] = 0xAA;

    if (specialOffset)
        ebr.part[0].starting_sector = 2048 + specialOffset;
    else
        ebr.part[0].starting_sector = ((((startOfExtended+offsetInSectors+specialOffset+2048)+6144)/8192)*8192) - (startOfExtended+offsetInSectors);

    ebr.part[0].nr_of_sectors = sizeInSectors;
    ebr.part[0].id = type;
    f.seek(qint64(startOfExtended+offsetInSectors)*512);
    f.write((char *) &ebr, sizeof(ebr));
    f.flush();
    /* Tell Linux to re-read the partition table */
    ioctl(f.handle(), BLKRRPART);
    f.close();

    /* Call partprobe just in case. BLKRRPART should be enough though */
    QProcess::execute("/usr/sbin/partprobe");
    QThread::msleep(500);

    /* Remount */
    //QProcess::execute("mount -o ro -t vfat /dev/mmcblk0p1 /mnt");
    //QProcess::execute("mount -t ext4 /dev/mmcblk0p3 /settings");

    return true;
}

bool MultiImageWriteThread::mkfs(const QByteArray &device, const QByteArray &fstype, const QByteArray &label, const QByteArray &mkfsopt)
{
    QString cmd;

    if (fstype == "fat" || fstype == "FAT")
    {
        cmd = "/sbin/mkfs.fat ";
        if (!label.isEmpty())
        {
            cmd += "-n "+label+" ";
        }
    }
    else if (fstype == "ext4")
    {
        cmd = "/usr/sbin/mkfs.ext4 ";
        if (!label.isEmpty())
        {
            cmd += "-L "+label+" ";
        }
    }

    if (!mkfsopt.isEmpty())
        cmd += mkfsopt+" ";

    cmd += device;

    qDebug() << "Executing:" << cmd;
    QProcess p;
    p.setProcessChannelMode(p.MergedChannels);
    p.start(cmd);
    p.closeWriteChannel();
    p.waitForFinished(-1);

    if (p.exitCode() != 0)
    {
        emit error(tr("Error creating file system")+"\n"+p.readAll());
        return false;
    }

    return true;
}

bool MultiImageWriteThread::isLabelAvailable(const QByteArray &label)
{
    return (QProcess::execute("/sbin/findfs LABEL="+label) != 0);
}

bool MultiImageWriteThread::untar(const QString &tarball)
{
    QString cmd = "sh -o pipefail -c \"";

    if (tarball.startsWith("http:"))
        cmd += "wget --no-verbose --tries=inf -O- "+tarball+" | ";

    if (tarball.endsWith(".gz"))
    {
        cmd += "gzip -dc";
    }
    else if (tarball.endsWith(".xz"))
    {
        cmd += "xz -dc";
    }
    else if (tarball.endsWith(".bz2"))
    {
        cmd += "bzip2 -dc";
    }
    else if (tarball.endsWith(".lzo"))
    {
        cmd += "lzop -dc";
    }
    else if (tarball.endsWith(".zip"))
    {
        /* Note: the image must be the only file inside the .zip */
        cmd += "unzip -p";
    }
    else
    {
        emit error(tr("Unknown compression format file extension. Expecting .lzo, .gz, .xz, .bz2 or .zip"));
        return false;
    }
    if (!tarball.startsWith("http:"))
    {
        cmd += " "+tarball;
    }
    cmd += " | tar x -C /mnt2 ";
    cmd += "\"";

    QTime t1;
    t1.start();
    qDebug() << "Executing:" << cmd;

    QProcess p;
    p.setProcessChannelMode(p.MergedChannels);
    p.start(cmd);
    p.closeWriteChannel();
    p.waitForFinished(-1);

    if (p.exitCode() != 0)
    {
        QByteArray msg = p.readAll();
        qDebug() << msg;
        emit error(tr("Error downloading or extracting tarball")+"\n"+msg);
        return false;
    }
    qDebug() << "finished writing filesystem in" << (t1.elapsed()/1000.0) << "seconds";

    return true;
}

bool MultiImageWriteThread::dd(const QString &imagePath, const QString &device)
{
    QString cmd = "sh -o pipefail -c \"";

    if (imagePath.startsWith("http:"))
        cmd += "wget --no-verbose --tries=inf -O- "+imagePath+" | ";

    if (imagePath.endsWith(".gz"))
    {
        cmd += "gzip -dc";
    }
    else if (imagePath.endsWith(".xz"))
    {
        cmd += "xz -dc";
    }
    else if (imagePath.endsWith(".bz2"))
    {
        cmd += "bzip2 -dc";
    }
    else if (imagePath.endsWith(".lzo"))
    {
        cmd += "lzop -dc";
    }
    else if (imagePath.endsWith(".zip"))
    {
        /* Note: the image must be the only file inside the .zip */
        cmd += "unzip -p";
    }
    else
    {
        emit error(tr("Unknown compression format file extension. Expecting .lzo, .gz, .xz, .bz2 or .zip"));
        return false;
    }

    if (!imagePath.startsWith("http:"))
        cmd += " "+imagePath;

    cmd += " | dd of="+device+" conv=fsync obs=4M\"";

    QTime t1;
    t1.start();
    qDebug() << "Executing:" << cmd;

    QProcess p;
    p.setProcessChannelMode(p.MergedChannels);
    p.start(cmd);
    p.closeWriteChannel();
    p.waitForFinished(-1);

    if (p.exitCode() != 0)
    {
        emit error(tr("Error downloading or writing OS to SD card")+"\n"+p.readAll());
        return false;
    }
    qDebug() << "finished writing filesystem in" << (t1.elapsed()/1000.0) << "seconds";

    return true;
}

void MultiImageWriteThread::patchConfigTxt()
{

        QSettings settings("/settings/noobs.conf", QSettings::IniFormat);
        int videomode = settings.value("display_mode", 0).toInt();

        QByteArray dispOptions;

        switch (videomode)
        {
        case 0: /* HDMI PREFERRED */
            dispOptions = "hdmi_force_hotplug=1\r\nconfig_hdmi_boost=4\r\noverscan_left=24\r\noverscan_right=24\r\noverscan_top=16\r\noverscan_bottom=16\r\ndisable_overscan=0\r\n";
            break;
        case 1: /* HDMI VGA */
            dispOptions = "hdmi_ignore_edid=0xa5000080\r\nhdmi_force_hotplug=1\r\nconfig_hdmi_boost=4\r\nhdmi_group=2\r\nhdmi_mode=4\r\n";
            break;
        case 2: /* PAL */
            dispOptions = "hdmi_ignore_hotplug=1\r\nsdtv_mode=2\r\n";
            break;
        case 3: /* NTSC */
            dispOptions = "hdmi_ignore_hotplug=1\r\nsdtv_mode=0\r\n";
            break;
        }


        QFile f("/mnt2/config.txt");
        f.open(f.Append);
        f.write("\r\n# NOOBS Auto-generated Settings:\r\n"+dispOptions);
        f.close();

}

QByteArray MultiImageWriteThread::getLabel(const QString part)
{
    QByteArray result;
    QProcess p;
    p.start("/sbin/blkid -s LABEL -o value "+part);
    p.waitForFinished();

    if (p.exitCode() == 0)
        result = p.readAll().trimmed();

    return result;
}

QByteArray MultiImageWriteThread::getUUID(const QString part)
{
    QByteArray result;
    QProcess p;
    p.start("/sbin/blkid -s UUID -o value "+part);
    p.waitForFinished();

    if (p.exitCode() == 0)
        result = p.readAll().trimmed();

    return result;
}

QString MultiImageWriteThread::getDescription(const QString &folder, const QString &flavour)
{
    if (QFile::exists(folder+"/flavours.json"))
    {
        QVariantMap v = Json::loadFromFile(folder+"/flavours.json").toMap();
        QVariantList fl = v.value("flavours").toList();

        foreach (QVariant f, fl)
        {
            QVariantMap fm  = f.toMap();
            if (fm.value("name").toString() == flavour)
            {
                return fm.value("description").toString();
            }
        }
    }
    else if (QFile::exists(folder+"/os.json"))
    {
        QVariantMap v = Json::loadFromFile(folder+"/os.json").toMap();
        return v.value("description").toString();
    }

    return "";
}
