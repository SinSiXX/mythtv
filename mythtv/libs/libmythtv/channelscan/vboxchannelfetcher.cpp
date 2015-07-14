// Std C headers
#include <cmath>
#include <unistd.h>

// Qt headers
#include <QFile>
#include <QTextStream>

// MythTV headers
#include "mythcontext.h"
#include "cardutil.h"
#include "channelutil.h"
#include "vboxchannelfetcher.h"
#include "scanmonitor.h"
#include "mythlogging.h"
#include "mythdownloadmanager.h"
#include "recorders/vboxutils.h"

#define LOC QString("VBoxChanFetch: ")

VBoxChannelFetcher::VBoxChannelFetcher(uint cardid, const QString &inputname,
                                       uint sourceid, ScanMonitor *monitor) :
    _scan_monitor(monitor),
    _cardid(cardid),       _inputname(inputname),
    _sourceid(sourceid),   _channels(NULL),
    _chan_cnt(1),          _thread_running(false),
    _stop_now(false),      _thread(new MThread("VBoxChannelFetcher", this)),
    _lock()
{
    LOG(VB_CHANNEL, LOG_INFO, LOC + QString("Has ScanMonitor %1")
        .arg(monitor?"true":"false"));
}

VBoxChannelFetcher::~VBoxChannelFetcher()
{
    Stop();
    delete _thread;
    _thread = NULL;
    delete _channels;
    _channels = NULL;
}

/** \fn VBoxChannelFetcher::Stop(void)
 *  \brief Stops the scanning thread running
 */
void VBoxChannelFetcher::Stop(void)
{
    _lock.lock();

    while (_thread_running)
    {
        _stop_now = true;
        _lock.unlock();
        _thread->wait(5);
        _lock.lock();
    }

    _lock.unlock();

    _thread->wait();
}

vbox_chan_map_t VBoxChannelFetcher::GetChannels(void)
{
    while (!_thread->isFinished())
        _thread->wait(500);

    LOG(VB_CHANNEL, LOG_INFO, LOC + QString("Found %1 channels")
        .arg(_channels->size()));
    return *_channels;
}

void VBoxChannelFetcher::Scan(void)
{
    Stop();
    _stop_now = false;
    _thread->start();
}

void VBoxChannelFetcher::run(void)
{
    _lock.lock();
    _thread_running = true;
    _lock.unlock();

    // Step 1/3 : Get the IP of the VBox to query
    QString dev = CardUtil::GetVideoDevice(_cardid);
    QString ip = VBox::getIPFromVideoDevice(dev);

    if (_stop_now || ip.isEmpty())
    {
        LOG(VB_CHANNEL, LOG_INFO, LOC +
            QString("Failed to get IP address from videodev (%1)").arg(dev));
        QMutexLocker locker(&_lock);
        _thread_running = false;
        _stop_now = true;
        return;
    }

    LOG(VB_CHANNEL, LOG_INFO, LOC + QString("VBox IP: %1").arg(ip));

    // Step 2/3 : Download
    if (_scan_monitor)
    {
        _scan_monitor->ScanPercentComplete(5);
        _scan_monitor->ScanAppendTextToLog(tr("Downloading Channel List"));
    }

    if (_channels)
        delete _channels;

    VBox *vbox = new VBox(ip);
    _channels = vbox->getChannels();
    delete vbox;

    if (_stop_now || !_channels)
    {
        if (!_channels && _scan_monitor)
        {
            _scan_monitor->ScanAppendTextToLog(QCoreApplication::translate("(Common)", "Error"));
            _scan_monitor->ScanPercentComplete(100);
            _scan_monitor->ScanErrored(tr("Downloading Channel List Failed"));
        }
        QMutexLocker locker(&_lock);
        _thread_running = false;
        _stop_now = true;
        return;
    }

    // Step 3/3 : Process
    if (_scan_monitor)
    {
        _scan_monitor->ScanPercentComplete(35);
        _scan_monitor->ScanAppendTextToLog(tr("Adding Channels"));
    }

    SetTotalNumChannels(_channels->size());

    LOG(VB_CHANNEL, LOG_INFO, LOC + QString("Found %1 channels")
        .arg(_channels->size()));

    // add the channels to the DB
    vbox_chan_map_t::const_iterator it = _channels->begin();
    for (uint i = 1; it != _channels->end(); ++it, ++i)
    {
        QString channum = it.key();
        QString name    = (*it).m_name;
        QString xmltvid = (*it).m_xmltvid.isEmpty() ? "" : (*it).m_xmltvid;
        uint serviceID = (*it).m_serviceID;
        //: %1 is the channel number, %2 is the channel name
        QString msg = tr("Channel #%1 : %2").arg(channum).arg(name);

        LOG(VB_CHANNEL, LOG_INFO, QString("Handling channel %1 %2")
            .arg(channum).arg(name));

        int chanid = ChannelUtil::GetChanID(_sourceid, channum);
        if (chanid <= 0)
        {
            if (_scan_monitor)
            {
                _scan_monitor->ScanAppendTextToLog(tr("Adding %1").arg(msg));
            }
            chanid = ChannelUtil::CreateChanID(_sourceid, channum);
            ChannelUtil::CreateChannel(0, _sourceid, chanid, name, name,
                                       channum, serviceID, 0, 0,
                                       false, false, false, QString::null,
                                       QString::null, "Default", xmltvid);
            ChannelUtil::CreateIPTVTuningData(chanid, (*it).m_tuning);
        }
        else
        {
            if (_scan_monitor)
            {
                _scan_monitor->ScanAppendTextToLog(
                                            tr("Updating %1").arg(msg));
            }
            ChannelUtil::UpdateChannel(0, _sourceid, chanid, name, name,
                                       channum, serviceID, 0, 0,
                                       false, false, false, QString::null,
                                       QString::null, "Default", xmltvid);
            ChannelUtil::UpdateIPTVTuningData(chanid, (*it).m_tuning);
        }

        SetNumChannelsInserted(i);
    }

    if (_scan_monitor)
    {
        _scan_monitor->ScanAppendTextToLog(tr("Done"));
        _scan_monitor->ScanAppendTextToLog("");
        _scan_monitor->ScanPercentComplete(100);
        _scan_monitor->ScanComplete();
    }

    QMutexLocker locker(&_lock);
    _thread_running = false;
    _stop_now = true;
}

void VBoxChannelFetcher::SetNumChannelsInserted(uint val)
{
    uint minval = 70, range = 100 - minval;
    uint pct = minval + (uint) truncf((((float)val) / _chan_cnt) * range);
    if (_scan_monitor)
        _scan_monitor->ScanPercentComplete(pct);
}
