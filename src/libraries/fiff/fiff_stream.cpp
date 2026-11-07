//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2012-2026 MNE-CPP Authors
 *
 * @file     fiff_stream.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>;
 *           Florian Schlembach <fschlembach@web.de>;
 *           Limin Sun <liminsun@nmr.mgh.harvard.edu>;
 *           Lorenz Esch <lorenz.esch@tu-ilmenau.de>;
 *           Eric Larson <larson.eric.d@gmail.com>;
 *           Juan GPC <jgarciaprieto@mgh.harvard.edu>;
 *           Gabriel Motta <gabrielbenmotta@gmail.com>;
 *           Ruben Doerfel <doerfelruben@aol.com>;
 *           Andreas Griesshammer <ag@fieldlineinc.com>
 * @since    0.1.0
 * @date     October 2012
 * @brief    Implementation of @ref FiffStream: FIFF tag I/O, directory and block-tree construction, and the typed read_* / write_* methods.
 *
 * Owns one @c QIODevice. Builds the directory and @ref FiffDirNode tree
 * on open; trades tag streams for typed FIFFLIB objects on read; emits
 * spec-compliant FIFF on write. Files emitted here round-trip through
 * @c mne.io.fiff unchanged.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "fiff_stream.h"
#include "fiff_annotations.h"
#include "fiff_tag.h"
#include "fiff_dir_node.h"
#include "fiff_ctf_comp.h"
#include "fiff_info.h"
#include "fiff_info_base.h"
#include "fiff_raw_data.h"
#include "fiff_cov.h"
#include "fiff_evoked_set.h"
#include "fiff_coord_trans.h"
#include "fiff_ch_info.h"
#include "fiff_ch_pos.h"
#include "fiff_dig_point.h"
#include "fiff_id.h"
#include "fiff_digitizer_data.h"
#include "fiff_dig_point.h"

#include <math/linalg.h>
#include <math/numerics.h>
#include <utils/ioutils.h>
#include <utils/string_utils.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <time.h>
#include <utility>

//=============================================================================================================
// EIGEN INCLUDES
//=============================================================================================================

#include <Eigen/LU>
#include <Eigen/Dense>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QDebug>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace FIFFLIB;
using UTILSLIB::ErrorCode;
using UTILSLIB::fail;
using namespace UTILSLIB;
using namespace Eigen;

//=============================================================================================================
// DEFINE LOCAL CLASSES
//=============================================================================================================

namespace
{

// Byte stream over a Qt device, so sockets and Qt files keep working
class QIODeviceByteStream : public UTILSLIB::ByteStream
{
public:
    explicit QIODeviceByteStream(QIODevice* device)
    : m_device(device)
    {
    }

    std::size_t read(void* data, std::size_t bytes) override
    {
        const qint64 n = m_device->read(static_cast<char*>(data), static_cast<qint64>(bytes));
        return n > 0 ? static_cast<std::size_t>(n) : 0;
    }

    std::size_t write(const void* data, std::size_t bytes) override
    {
        const qint64 n = m_device->write(static_cast<const char*>(data), static_cast<qint64>(bytes));
        return n > 0 ? static_cast<std::size_t>(n) : 0;
    }

    bool seek(std::uint64_t position) override
    {
        return m_device->seek(static_cast<qint64>(position));
    }

    std::uint64_t pos() const override
    {
        return static_cast<std::uint64_t>(m_device->pos());
    }

    std::uint64_t size() const override
    {
        return static_cast<std::uint64_t>(m_device->size());
    }

    void close() override
    {
        m_writable = m_device->isWritable();
        m_device->close();
    }

    bool isOpen() const override
    {
        return m_device->isOpen();
    }

    bool reopen() override
    {
        // Reopening never truncates: a device written to comes back read-write
        return m_device->isOpen() || m_device->open(m_writable ? QIODevice::ReadWrite : QIODevice::ReadOnly);
    }

private:
    QIODevice* m_device;
    bool m_writable = false;
};

} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

FiffStream::FiffStream(QIODevice* p_pIODevice)
: m_device(p_pIODevice)
, m_bytes(std::make_unique<QIODeviceByteStream>(p_pIODevice))
{
}

//=============================================================================================================

FiffStream::FiffStream(QByteArray* a,
                       QIODevice::OpenMode mode)
: m_ownedDevice(std::make_unique<QBuffer>(a))
, m_device(m_ownedDevice.get())
, m_bytes(std::make_unique<QIODeviceByteStream>(m_device))
{
    m_device->open(mode);
}

//=============================================================================================================

FiffStream::FiffStream(std::unique_ptr<UTILSLIB::ByteStream> bytes, std::string name)
: m_bytes(std::move(bytes))
, m_name(std::move(name))
{
}

//=============================================================================================================

UTILSLIB::Expected<FiffStream> FiffStream::open(const std::filesystem::path& path)
{
    const std::string name = UTILSLIB::utf8FromPath(path);
    auto bytes = UTILSLIB::FileByteStream::open(path, UTILSLIB::FileByteStream::Mode::Read);
    if (!bytes) {
        return fail(ErrorCode::Io, "Cannot open {}", name);
    }
    FiffStream stream(std::move(bytes), name);
    if (!stream.open()) {
        return fail(ErrorCode::Format, "{} is not a FIFF file", name);
    }
    return stream;
}

//=============================================================================================================

std::unique_ptr<FiffStream> FiffStream::start_file(const std::filesystem::path& path)
{
    auto bytes = UTILSLIB::FileByteStream::open(path, UTILSLIB::FileByteStream::Mode::Write);
    if (!bytes) {
        qWarning("Cannot write to %s", UTILSLIB::utf8FromPath(path).c_str());
        return {};
    }
    return start_file(std::make_unique<FiffStream>(std::move(bytes), UTILSLIB::utf8FromPath(path)));
}

//=============================================================================================================

std::unique_ptr<FiffStream> FiffStream::start_file(std::unique_ptr<FiffStream> stream)
{
    stream->write_id(FIFF_FILE_ID);
    int null_pointer = FIFFV_NEXT_NONE;
    stream->write_int(FIFF_DIR_POINTER, &null_pointer);
    stream->write_int(FIFF_FREE_LIST, &null_pointer);
    return stream;
}

//=============================================================================================================

std::unique_ptr<UTILSLIB::ByteStream> FiffStream::byteStream(QIODevice& device, QIODevice::OpenMode mode)
{
    if (device.isOpen() ? (device.openMode() & mode) != mode : !device.open(mode))
        return {};
    return std::make_unique<QIODeviceByteStream>(&device);
}

//=============================================================================================================

FiffStream::~FiffStream() = default;

FiffStream::FiffStream(FiffStream&&) noexcept = default;

FiffStream& FiffStream::operator=(FiffStream&&) noexcept = default;

//=============================================================================================================

QIODevice* FiffStream::device() const
{
    return m_device;
}

//=============================================================================================================

bool FiffStream::isOpen() const
{
    return m_bytes && m_bytes->isOpen();
}

//=============================================================================================================

bool FiffStream::reopen()
{
    if (isOpen())
        return true;
    return m_bytes && m_bytes->reopen();
}

//=============================================================================================================

void FiffStream::setLittleEndian(bool littleEndian)
{
    m_littleEndian = littleEndian;
}

//=============================================================================================================

bool FiffStream::isLittleEndian() const
{
    return m_littleEndian;
}

//=============================================================================================================

fiff_long_t FiffStream::tell() const
{
    return static_cast<fiff_long_t>(m_bytes->pos());
}

//=============================================================================================================

bool FiffStream::seekTo(fiff_long_t pos)
{
    return pos >= 0 && m_bytes->seek(static_cast<std::uint64_t>(pos));
}

//=============================================================================================================

void FiffStream::seekToEnd()
{
    // Appending tags without a position goes to the end of files; other Qt devices write where they are
    if (!m_device || qobject_cast<QFile*>(m_device))
        m_bytes->seek(m_bytes->size());
}

//=============================================================================================================

void FiffStream::readRaw(void* data, fiff_long_t bytes)
{
    if (bytes > 0 && m_bytes->read(data, static_cast<std::size_t>(bytes)) != static_cast<std::size_t>(bytes))
        m_readFailed = true;
}

//=============================================================================================================

void FiffStream::writeRaw(const void* data, fiff_long_t bytes)
{
    if (bytes > 0)
        m_bytes->write(data, static_cast<std::size_t>(bytes));
}

//=============================================================================================================

void FiffStream::skipRaw(fiff_long_t bytes)
{
    if (m_device && m_device->isSequential())
        m_device->skip(bytes);
    else
        seekTo(tell() + bytes);
}

//=============================================================================================================

QString FiffStream::streamName()
{
    if (QFile* t_pFile = qobject_cast<QFile*>(this->device()))
        return t_pFile->fileName();
    return m_name.empty() ? QString("TCPSocket") : QString::fromStdString(m_name);
}

//=============================================================================================================

FiffId FiffStream::id() const
{
    return m_id;
}

//=============================================================================================================

std::vector<FiffDirEntry>& FiffStream::dir()
{
    return m_dir;
}

//=============================================================================================================

const std::vector<FiffDirEntry>& FiffStream::dir() const
{
    return m_dir;
}

//=============================================================================================================

int FiffStream::nent() const
{
    return static_cast<int>(m_dir.size());
}

//=============================================================================================================

const FiffDirNode::SPtr& FiffStream::dirtree() const
{
    return m_dirtree;
}

//=============================================================================================================

fiff_long_t FiffStream::end_block(fiff_int_t kind, fiff_int_t next)
{
    return this->write_int(FIFF_BLOCK_END, &kind, 1, next);
}

//=============================================================================================================

void FiffStream::end_file()
{
    fiff_int_t datasize = 0;

    write(static_cast<qint32>(FIFF_NOP));
    write(static_cast<qint32>(FIFFT_VOID));
    write(static_cast<qint32>(datasize));
    write(static_cast<qint32>(FIFFV_NEXT_NONE));
}

//=============================================================================================================

void FiffStream::finish_writing_raw()
{
    this->end_block(FIFFB_RAW_DATA);
    this->end_block(FIFFB_MEAS);
    this->end_file();
    this->close();
}

//=============================================================================================================

bool FiffStream::get_evoked_entries(const std::vector<FiffDirNode::SPtr>& evoked_node, QStringList& comments, QList<fiff_int_t>& aspect_kinds, QString& t)
{
    comments.clear();
    aspect_kinds.clear();
    std::optional<FiffTag> t_pTag;
    qint32 k;

    for (const FiffDirNode::SPtr& ev : evoked_node) {
        for (const FiffDirEntry& entry : ev->dir) {
            if (entry.kind == FIFF_COMMENT) {
                t_pTag = this->read_tag(entry.pos);
                comments.append(QString::fromStdString(t_pTag->toString()));
            }
        }
        const std::vector<FiffDirNode::SPtr> aspects = ev->dir_tree_find(FIFFB_ASPECT);
        if (aspects.empty()) {
            continue;
        }
        for (const FiffDirEntry& entry : aspects.front()->dir) {
            if (entry.kind == FIFF_ASPECT_KIND) {
                t_pTag = this->read_tag(entry.pos);
                aspect_kinds.append(*t_pTag->toInt());
            }
        }
    }

    if (comments.size() != aspect_kinds.size() || comments.size() == 0) {
        qWarning("Dataset names in FIF file could not be found.");
        return false;
    }

    t = QString();
    for (k = 0; k < aspect_kinds.size(); ++k) {
        t += QString("%1 - \"%2\" (").arg(k).arg(comments[k]);
        if (aspect_kinds[k] == FIFFV_ASPECT_AVERAGE)
            t += QString("FIFFV_ASPECT_AVERAGE)\n");
        else if (aspect_kinds[k] == FIFFV_ASPECT_STD_ERR)
            t += QString("FIFFV_ASPECT_STD_ERR)\n");
        else
            t += QString("unknown)\n");
    }

    return true;
}

//=============================================================================================================

bool FiffStream::open(QIODevice::OpenModeFlag mode)
{
    QString t_sFileName = this->streamName();
    std::optional<FiffTag> t_pTag;

    /*
     * Try to open...
     */
    if (m_device ? !m_device->open(mode) : !reopen()) {
        qCritical("Cannot open %s\n", t_sFileName.toUtf8().constData()); //consider throw
        return false;
    }

    if (!(t_pTag = check_beginning())) // Supposed to get the id already in the beginning - read once approach - for TCP/IP support
        return false;

    /*
     * Read id and directory pointer
     */
    if (t_pTag->kind != FIFF_FILE_ID) {
        qCritical("FIFF file should start with FIFF_FILE_ID!"); //consider throw
        close();
        return false;
    }
    m_id = t_pTag->toFiffID();

    t_pTag = this->read_tag();
    if (t_pTag->kind != FIFF_DIR_POINTER) {
        qWarning("Fiff::open: file does have a directory pointer"); //consider throw
        close();
        return false;
    }

    //
    //   Read or create the directory tree
    //
    qInfo("Creating tag directory for %s...", t_sFileName.toUtf8().constData());
    m_dir.clear();
    qint32 dirpos = *t_pTag->toInt();
    /*
     * Do we have a directory or not?
     */
    if (dirpos <= 0) { /* Must do it in the hard way... */
        bool ok = false;
        m_dir = this->make_dir(&ok);
        if (!ok) {
            qCritical("Could not create tag directory!");
            return false;
        }
    } else { /* Just read the directory */
        if (t_pTag = this->read_tag(dirpos); !t_pTag) {
            qCritical("Could not read the tag directory (file probably damaged)!");
            return false;
        }
        m_dir = t_pTag->toDirEntry();
    }

    /*
     * Check for a mistake
     */
    if (m_dir[m_dir.size() - 2].kind == FIFF_DIR) {
        m_dir.pop_back();
        m_dir.back() = FiffDirEntry{};
    }

    //
    //   Create the directory tree structure
    //
    if ((this->m_dirtree = this->make_subtree(m_dir, 0)) == nullptr)
        return false;
    else
        this->m_dirtree->parent.reset();

    //
    //   Back to the beginning
    //
    seekTo(SEEK_SET); //fseek(fid,0,'bof');
    return true;
}

//=============================================================================================================

bool FiffStream::close()
{
    if (m_bytes && m_bytes->isOpen())
        m_bytes->close();
    return true;
}

//=============================================================================================================

FiffDirNode::SPtr FiffStream::make_subtree(const std::vector<FiffDirEntry>& dentry, qsizetype current)
{
    FiffDirNode::SPtr defaultNode;
    auto node = std::make_shared<FiffDirNode>();
    FiffDirNode::SPtr child;
    std::optional<FiffTag> t_pTag;
    std::vector<FiffDirEntry> dir;

    node->type = FIFFB_ROOT;

    if (dentry[current].kind == FIFF_BLOCK_START) {
        if (t_pTag = this->read_tag(dentry[current].pos); !t_pTag)
            return defaultNode;
        else
            node->type = *t_pTag->toInt();
    } else {
        node->id = this->id();
    }

    ++current;
    int level = 0;
    for (; current < std::ssize(dentry); ++current) {
        if (dentry[current].kind == FIFF_BLOCK_START) {
            level++;
            if (level == 1) {
                if (child = this->make_subtree(dentry, current); !child)
                    return defaultNode;
                child->parent = node;
                node->children.push_back(child);
            }
        } else if (dentry[current].kind == FIFF_BLOCK_END) {
            level--;
            if (level < 0)
                break;
        } else if (dentry[current].kind == -1)
            break;
        else if (level == 0) {
            /*
            * Take the node id from the parent block id,
            * block id, or file id. Let the block id
            * take precedence over parent block id and file id
            */
            if (((dentry[current].kind == FIFF_PARENT_BLOCK_ID || dentry[current].kind == FIFF_FILE_ID) && node->id.isEmpty()) || dentry[current].kind == FIFF_BLOCK_ID) {
                if (t_pTag = this->read_tag(dentry[current].pos); !t_pTag)
                    return defaultNode;
                node->id = t_pTag->toFiffID();
            }
            dir.push_back(dentry[current]);
        }
    }
    /*
     * Strip unused entries
     */
    node->dir = dir;
    return node;
}

//=============================================================================================================

StringList FiffStream::read_bad_channels(const FiffDirNode::SPtr& p_Node)
{
    std::vector<FiffDirNode::SPtr> node = p_Node->dir_tree_find(FIFFB_MNE_BAD_CHANNELS);
    std::optional<FiffTag> t_pTag;

    StringList bads;

    if (node.size() > 0)
        if (t_pTag = node[0]->find_tag(*this, FIFF_MNE_CH_NAME_LIST); t_pTag)
            bads = split_name_list(t_pTag->toString());

    return bads;
}

//=============================================================================================================

void FiffStream::write_bad_channels(const StringList& bads)
{
    start_block(FIFFB_MNE_BAD_CHANNELS);
    write_name_list(FIFF_MNE_CH_NAME_LIST, bads);
    end_block(FIFFB_MNE_BAD_CHANNELS);
}

//=============================================================================================================

bool FiffStream::read_annotations(const FiffDirNode::SPtr& p_Node, FiffAnnotations& annotations, fiff_int_t origTime[2])
{
    annotations.clear();
    origTime[0] = -1;
    origTime[1] = 0;
    const std::vector<FiffDirNode::SPtr> blocks = p_Node->dir_tree_find(FIFFB_MNE_ANNOTATIONS);
    if (blocks.empty()) {
        return false;
    }
    QVector<float> onsets;
    QVector<float> ends;
    QStringList descriptions;
    QJsonArray channelNames;
    QJsonArray extras;
    std::optional<FiffTag> tag;
    for (const FiffDirEntry& entry : blocks.front()->dir) {
        if (tag = this->read_tag(entry.pos); !tag) {
            continue;
        }
        switch (entry.kind) {
            case FIFF_MNE_BASELINE_MIN:
                onsets = QVector<float>(tag->toFloat(), tag->toFloat() + tag->size() / 4);
                break;
            case FIFF_MNE_BASELINE_MAX:
                ends = QVector<float>(tag->toFloat(), tag->toFloat() + tag->size() / 4);
                break;
            case FIFF_COMMENT:
                // Unlike channel name lists, descriptions keep spaces; colons inside them are stored as {COLON}.
                if (!tag->toString().empty()) {
                    for (const std::string& name : UTILSLIB::split(tag->toString(), ':')) {
                        descriptions << QString::fromStdString(name).replace(QStringLiteral("{COLON}"), QStringLiteral(":"));
                    }
                }
                break;
            case FIFF_MEAS_DATE:
                if (tag->getType() == FIFFT_DOUBLE && tag->size() >= 16) {
                    origTime[0] = static_cast<fiff_int_t>(tag->toDouble()[0]);
                    origTime[1] = static_cast<fiff_int_t>(tag->toDouble()[1]);
                } else if (tag->getType() == FIFFT_DOUBLE && tag->size() == 8) {
                    const double stamp = tag->toDouble()[0];
                    origTime[0] = static_cast<fiff_int_t>(std::floor(stamp));
                    origTime[1] = static_cast<fiff_int_t>(std::lround((stamp - std::floor(stamp)) * 1e6));
                }
                break;
            case FIFF_MNE_EPOCHS_DROP_LOG:
                channelNames = QJsonDocument::fromJson(QByteArray::fromStdString(tag->toString())).array();
                break;
            case FIFF_FREE_LIST:
                extras = QJsonDocument::fromJson(QByteArray::fromStdString(tag->toString())).array();
                break;
            default:
                break;
        }
    }
    const int count = std::min({static_cast<int>(onsets.size()), static_cast<int>(ends.size()),
                                static_cast<int>(descriptions.size())});
    for (int i = 0; i < count; ++i) {
        FiffAnnotation annotation;
        annotation.onset = static_cast<double>(onsets[i]);
        annotation.duration = static_cast<double>(ends[i] - onsets[i]); // float32, as MNE-Python
        annotation.description = descriptions[i];
        for (const auto& name : channelNames.at(i).toArray()) {
            annotation.channelNames << name.toString();
        }
        annotation.extras = extras.at(i).toObject().toVariantMap();
        annotations.append(annotation);
    }
    return true;
}

//=============================================================================================================

fiff_long_t FiffStream::write_annotations(const FiffAnnotations& annotations, const fiff_int_t* origTime)
{
    const fiff_long_t pos = start_block(FIFFB_MNE_ANNOTATIONS);
    const int count = annotations.size();
    QVector<float> onsets(count);
    QVector<float> ends(count);
    QStringList descriptions;
    QJsonArray channelNames;
    QJsonArray extras;
    bool anyChannels = false;
    bool anyExtras = false;
    for (int i = 0; i < count; ++i) {
        const FiffAnnotation& annotation = annotations[i];
        onsets[i] = static_cast<float>(annotation.onset);
        ends[i] = static_cast<float>(annotation.onset + annotation.duration);
        descriptions << QString(annotation.description).replace(QLatin1Char(':'), QStringLiteral("{COLON}"));
        channelNames.append(QJsonArray::fromStringList(annotation.channelNames));
        extras.append(QJsonObject::fromVariantMap(annotation.extras));
        anyChannels = anyChannels || !annotation.channelNames.isEmpty();
        anyExtras = anyExtras || !annotation.extras.isEmpty();
    }
    write_float(FIFF_MNE_BASELINE_MIN, onsets.constData(), count);
    write_float(FIFF_MNE_BASELINE_MAX, ends.constData(), count);
    write_string(FIFF_COMMENT, (descriptions.join(QLatin1Char(':'))).toStdString());
    if (origTime != nullptr) {
        const double stamp[2] = {static_cast<double>(origTime[0]), static_cast<double>(origTime[1])};
        write_double(FIFF_MEAS_DATE, stamp, 2);
    }
    if (anyChannels) {
        write_string(FIFF_MNE_EPOCHS_DROP_LOG, QString::fromUtf8(QJsonDocument(channelNames).toJson(QJsonDocument::Compact)).toStdString());
    }
    if (anyExtras) {
        write_string(FIFF_FREE_LIST, QString::fromUtf8(QJsonDocument(extras).toJson(QJsonDocument::Compact)).toStdString());
    }
    end_block(FIFFB_MNE_ANNOTATIONS);
    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_device_info(const FiffDeviceInfo& device)
{
    if (device.isEmpty()) {
        return -1;
    }
    const fiff_long_t pos = start_block(FIFFB_DEVICE);
    const std::pair<fiff_int_t, const std::string*> fields[] = {{FIFF_DEVICE_TYPE, &device.type},
                                                                {FIFF_DEVICE_MODEL, &device.model},
                                                                {FIFF_DEVICE_SERIAL, &device.serial},
                                                                {FIFF_DEVICE_SITE, &device.site}};
    for (const auto& [kind, value] : fields) {
        if (!value->empty()) {
            write_string(kind, *value);
        }
    }
    end_block(FIFFB_DEVICE);
    return pos;
}

//=============================================================================================================

bool FiffStream::attach_env(const QString& workingDir, const QString& command)
{
    int insert_blocks[] = {FIFFB_MNE, FIFFB_MEAS, FIFFB_MRI, FIFFB_BEM, -1};

    int b, k, insert;
    std::optional<FiffTag> t_pTag;

    FiffId id(FiffId::new_file_id());

    /*
     * Find an appropriate position to insert
     */
    for (insert = -1, b = 0; insert_blocks[b] >= 0; b++) {
        for (k = 0; k < nent(); k++) {
            if (dir()[k].kind == FIFF_BLOCK_START) {
                if (t_pTag = read_tag(dir()[k].pos); !t_pTag)
                    return false;
                if (*(t_pTag->toInt()) == insert_blocks[b]) {
                    insert = k;
                    break;
                }
            }
        }
        if (insert >= 0)
            break;
    }
    if (insert < 0) {
        qCritical("Suitable place for environment insertion not found.");
        return false;
    }

    /*
     * Inline fiff_insert_after logic
     */
    int where = insert;
    if (where < 0 || where >= nent() - 1) {
        qCritical("illegal insertion point in fiff_insert_after!");
        return false;
    }

    std::optional<FiffTag> t_pTagNext;
    const std::vector<FiffDirEntry> old_dir = dir();
    const FiffDirEntry this_ent = old_dir[static_cast<std::size_t>(where)];

    if (t_pTagNext = read_tag(this_ent.pos); !t_pTagNext)
        return false;
    /*
     * Update next info to be sequential
     */
    qint64 next_tmp = device()->pos();
    /*
     * Go to the end of the file
     */
    device()->seek(device()->size());
    /*
     * Copy the beginning of old directory
     */
    std::vector<FiffDirEntry> new_dir(old_dir.begin(), old_dir.begin() + where + 1);

    qint64 old_end = device()->pos();
    /*
     * Write the MNE_ENV block tags
     */
    new_dir.push_back({FIFF_BLOCK_START, FIFFT_INT, 1 * 4, static_cast<fiff_int_t>(start_block(FIFFB_MNE_ENV))});
    new_dir.push_back({FIFF_BLOCK_ID, FIFFT_ID_STRUCT, 5 * 4, static_cast<fiff_int_t>(write_id(FIFF_BLOCK_ID, id))});
    new_dir.push_back({FIFF_MNE_ENV_WORKING_DIR, FIFFT_STRING, static_cast<fiff_int_t>(workingDir.size()),
                       static_cast<fiff_int_t>(write_string(FIFF_MNE_ENV_WORKING_DIR, workingDir.toStdString()))});
    new_dir.push_back({FIFF_MNE_ENV_COMMAND_LINE, FIFFT_STRING, static_cast<fiff_int_t>(command.size()),
                       static_cast<fiff_int_t>(write_string(FIFF_MNE_ENV_COMMAND_LINE, command.toStdString()))});
    new_dir.push_back({FIFF_BLOCK_END, FIFFT_INT, 1 * 4, static_cast<fiff_int_t>(end_block(FIFFB_MNE_ENV, next_tmp))});
    new_dir.insert(new_dir.end(), old_dir.begin() + where + 1, old_dir.end());
    /*
     * Update the branching tag
     */
    t_pTagNext->next = static_cast<qint32>(old_end);
    write_tag(*t_pTagNext, this_ent.pos);

    dir() = new_dir;

    return true;
}

//=============================================================================================================

bool FiffStream::read_cov(const FiffDirNode::SPtr& p_Node, fiff_int_t cov_kind, FiffCov& p_covData)
{
    p_covData.clear();

    //
    //   Find all covariance matrices
    //
    std::vector<FiffDirNode::SPtr> covs = p_Node->dir_tree_find(FIFFB_MNE_COV);
    if (covs.size() == 0) {
        qWarning("No covariance matrices found");
        return false;
    }
    //
    //   Is any of the covariance matrices a noise covariance
    //
    qint32 p = 0;
    std::optional<FiffTag> tag;
    bool success = false;
    fiff_int_t dim, nfree, nn;
    StringList names;
    bool diagmat = false;
    VectorXd eig;
    MatrixXd eigvec;
    VectorXd cov_diag;
    MatrixXd cov;
    VectorXd cov_sparse;
    StringList bads;
    for (p = 0; p < std::ssize(covs); ++p) {
        tag = covs[p]->find_tag(*this, FIFF_MNE_COV_KIND);
        success = tag.has_value();
        if (success && *tag->toInt() == cov_kind) {
            FiffDirNode::SPtr current = covs[p];
            //
            //   Find all the necessary data
            //
            if (tag = current->find_tag(*this, FIFF_MNE_COV_DIM); !tag) {
                qWarning("Covariance matrix dimension not found.\n");
                return false;
            }
            dim = *tag->toInt();
            if (tag = current->find_tag(*this, FIFF_MNE_COV_NFREE); !tag)
                nfree = -1;
            else
                nfree = *tag->toInt();

            if (tag = current->find_tag(*this, FIFF_MNE_ROW_NAMES); tag) {
                names = FiffStream::split_name_list(tag->toString());
                if (static_cast<int>(names.size()) != dim) {
                    qWarning("Number of names does not match covariance matrix dimension\n");
                    return false;
                }
            }
            if (tag = current->find_tag(*this, FIFF_MNE_COV); !tag) {
                if (tag = current->find_tag(*this, FIFF_MNE_COV_DIAG); !tag) {
                    qWarning("No covariance matrix data found\n");
                    return false;
                } else {
                    //
                    //   Diagonal is stored
                    //
                    if (tag->type == FIFFT_DOUBLE) {
                        cov_diag = Map<const VectorXd>(tag->toDouble(), dim);
                    } else if (tag->type == FIFFT_FLOAT) {
                        cov_diag = Map<const VectorXf>(tag->toFloat(), dim).cast<double>();
                    } else {
                        qCritical("Illegal data type for covariance matrix\n");
                        return false;
                    }

                    diagmat = true;
                    qInfo("\t%d x %d diagonal covariance (kind = %d) found.\n", dim, dim, cov_kind);
                }
            } else {
                VectorXd vals;
                nn = dim * (dim + 1) / 2;
                if (tag->type == FIFFT_DOUBLE) {
                    vals = Map<const VectorXd>(tag->toDouble(), nn);
                } else if (tag->type == FIFFT_FLOAT) {
                    vals = Map<const VectorXf>(tag->toFloat(), nn).cast<double>();
                } else {
                    qDebug() << tag->getInfo();
                    return false;
                }

                if (!Numerics::issparse(vals)) {
                    //
                    //   Lower diagonal is stored
                    //
                    cov = MatrixXd::Zero(dim, dim);

                    // XXX : should remove for loops
                    qint32 q = 0;
                    for (qint32 j = 0; j < dim; ++j) {
                        for (qint32 k = 0; k <= j; ++k) {
                            cov(j, k) = vals(q);
                            ++q;
                        }
                    }
                    for (qint32 j = 0; j < dim; ++j)
                        for (qint32 k = j + 1; k < dim; ++k)
                            cov(j, k) = cov(k, j);

                    diagmat = false;
                    qInfo("\t%d x %d full covariance (kind = %d) found.\n", dim, dim, cov_kind);

                } else {
                    diagmat = false;
                    qDebug() << "ToDo: FiffStream::read_cov - this needs to be debugged.\n";
                    cov = vals;
                    qInfo("\t%d x %d sparse covariance (kind = %d) found.\n", dim, dim, cov_kind);
                }
                //MATLAB
                //                    if ~issparse(tag.data)
                //                        //
                //                        //   Lower diagonal is stored
                //                        //
                //                        qDebug() << tag->getInfo();
                //                        vals = tag.data;
                //                        data = zeros(dim,dim);
                //                        % XXX : should remove for loops
                //                        q = 1;
                //                        for j = 1:dim
                //                            for k = 1:j
                //                                data(j,k) = vals(q);
                //                                q = q + 1;
                //                            end
                //                        end
                //                        for j = 1:dim
                //                            for k = j+1:dim
                //                                data(j,k) = data(k,j);
                //                            end
                //                        end
                //                        diagmat = false;
                //                        fprintf('\t%d x %d full covariance (kind = %d) found.\n',dim,dim,cov_kind);
                //                    else
                //                        diagmat = false;
                //                        data = tag.data;
                //                        fprintf('\t%d x %d sparse covariance (kind = %d) found.\n',dim,dim,cov_kind);
                //                    end
                //MATLAB END
            }
            //
            //   Read the possibly precomputed decomposition
            //
            const auto tag1 = current->find_tag(*this, FIFF_MNE_COV_EIGENVALUES);
            const auto tag2 = tag1 ? current->find_tag(*this, FIFF_MNE_COV_EIGENVECTORS) : std::nullopt;
            if (tag1 && tag2) {
                eig = VectorXd(Map<const VectorXd>(tag1->toDouble(), dim));
                eigvec = tag2->toFloatMatrix().cast<double>();
                eigvec.transposeInPlace();
            }
            //
            //   Read the projection operator
            //
            QList<FiffProj> projs = this->read_proj(current);
            //
            //   Read the bad channel list
            //
            bads = this->read_bad_channels(current);
            //
            //   Put it together
            //
            p_covData.clear();

            p_covData.kind = cov_kind;
            p_covData.diag = diagmat;
            p_covData.dim = dim;
            p_covData.names = names;

            if (cov_diag.size() > 0)
                p_covData.data = cov_diag;
            else if (cov.size() > 0)
                p_covData.data = cov;
            else if (cov_sparse.size() > 0)
                p_covData.data = cov_sparse;

            p_covData.projs = projs;
            p_covData.bads = bads;
            p_covData.nfree = nfree;
            p_covData.eig = eig;
            p_covData.eigvec = eigvec;

            //
            return true;
        }
    }

    qInfo("Did not find the desired covariance matrix\n");
    return false;
}

//=============================================================================================================

QList<FiffCtfComp> FiffStream::read_ctf_comp(const FiffDirNode::SPtr& p_Node, const QList<FiffChInfo>& p_Chs)
{
    QList<FiffCtfComp> compdata;
    std::vector<FiffDirNode::SPtr> t_qListComps = p_Node->dir_tree_find(FIFFB_MNE_CTF_COMP_DATA);

    qint32 k, p;
    fiff_int_t kind, pos;
    std::optional<FiffTag> t_pTag;
    for (k = 0; k < std::ssize(t_qListComps); ++k) {
        FiffDirNode::SPtr node = t_qListComps[k];
        //
        //   Read the data we need
        //
        FiffNamedMatrix::SDPtr mat(new FiffNamedMatrix());
        this->read_named_matrix(node, FIFF_MNE_CTF_COMP_DATA, *mat);
        for (p = 0; p < node->nent(); ++p) {
            kind = node->dir[p].kind;
            pos = node->dir[p].pos;
            if (kind == FIFF_MNE_CTF_COMP_KIND) {
                t_pTag = this->read_tag(pos);
                break;
            }
        }
        if (!t_pTag) {
            qWarning("Compensation type not found\n");
            return {};
        }
        //
        //   Get the compensation kind and map it to a simple number
        //
        FiffCtfComp one;
        one.ctfkind = *t_pTag->toInt();

        t_pTag.reset();

        one.kind = -1;
        if (one.ctfkind == 1194410578) //hex2dec('47314252')
            one.kind = 1;
        else if (one.ctfkind == 1194476114) //hex2dec('47324252')
            one.kind = 2;
        else if (one.ctfkind == 1194541650) //hex2dec('47334252')
            one.kind = 3;
        else if (one.ctfkind == 1194479433)
            one.kind = 4;
        else if (one.ctfkind == 1194544969)
            one.kind = 5;
        else
            one.kind = one.ctfkind;

        for (p = 0; p < node->nent(); ++p) {
            kind = node->dir[p].kind;
            pos = node->dir[p].pos;
            if (kind == FIFF_MNE_CTF_COMP_CALIBRATED) {
                t_pTag = this->read_tag(pos);
                break;
            }
        }
        bool calibrated;
        if (!t_pTag)
            calibrated = false;
        else
            calibrated = static_cast<bool>(*t_pTag->toInt());

        one.save_calibrated = calibrated;
        one.data = mat;
        if (!one.calibrate(p_Chs))
            return {};
        compdata.append(one);
    }

    if (compdata.size() > 0)
        qInfo("\tRead %lld compensation matrices\n", static_cast<long long>(compdata.size()));

    return compdata;
}

//=============================================================================================================

bool FiffStream::read_digitizer_data(const FiffDirNode::SPtr& p_Node, FiffDigitizerData& p_digData)
{
    p_digData.coord_frame = FIFFV_COORD_UNKNOWN;
    fiff_int_t kind = -1;
    fiff_int_t pos = -1;
    int npoint = 0;
    std::optional<FiffTag> t_pTag;

    std::vector<FiffDirNode::SPtr> t_qListDigData = p_Node->dir_tree_find(FIFFB_ISOTRAK);

    //Check if digitizer data is available
    if (t_qListDigData.empty()) {
        t_qListDigData = p_Node->dir_tree_find(FIFFB_MRI_SET);

        if (t_qListDigData.empty()) {
            qWarning("No Isotrak data found in %s", this->streamName().toLatin1().data());
            return false;
        } else {
            p_digData.coord_frame = FIFFV_COORD_MRI;
        }
    } else {
        p_digData.coord_frame = FIFFV_COORD_HEAD;
    }

    // Read actual data and store it
    for (int k = 0; k < t_qListDigData.front()->nent(); ++k) {
        kind = t_qListDigData.front()->dir[k].kind;
        pos = t_qListDigData.front()->dir[k].pos;

        switch (kind) {
            case FIFF_DIG_POINT:
                t_pTag = this->read_tag(pos);
                p_digData.points.append(t_pTag->toDigPoint());
                break;
            case FIFF_MNE_COORD_FRAME:
                t_pTag = this->read_tag(pos);
                p_digData.coord_frame = *t_pTag->toInt();
                break;
        }
    }

    npoint = p_digData.points.size();

    if (npoint == 0) {
        qWarning("No digitizer data in %s", this->streamName().toLatin1().data());
        return false;
    }

    for (auto& dig : p_digData.points) {
        dig.coord_frame = p_digData.coord_frame;
    }

    //Add other information as default
    p_digData.filename = this->streamName();
    p_digData.npoint = npoint;

    for (int k = 0; k < p_digData.npoint; k++) {
        p_digData.active.append(1);
        p_digData.discard.append(0);
    }

    return true;
}

//=============================================================================================================

bool FiffStream::read_meas_info_base(const FiffDirNode::SPtr& p_Node, FiffInfoBase& p_InfoForward)
{
    p_InfoForward.clear();

    //
    //   Find the desired blocks
    //
    std::vector<FiffDirNode::SPtr> parent_meg = p_Node->dir_tree_find(FIFFB_MNE_PARENT_MEAS_FILE);

    if (parent_meg.size() == 0) {
        qWarning("No parent MEG information found in operator\n");
        return false;
    }

    std::optional<FiffTag> t_pTag;

    QList<FiffChInfo> chs;
    FiffCoordTrans cand;
    fiff_int_t kind = -1;
    fiff_int_t pos = -1;

    for (qint32 k = 0; k < parent_meg[0]->nent(); ++k) {
        kind = parent_meg[0]->dir[k].kind;
        pos = parent_meg[0]->dir[k].pos;
        if (kind == FIFF_CH_INFO) {
            t_pTag = this->read_tag(pos);
            chs.append(t_pTag->toChInfo());
        }
    }

    //
    //   Add the channel information and make a list of channel names
    //   for convenience
    //
    p_InfoForward.chs = chs;
    for (qint32 c = 0; c < p_InfoForward.chs.size(); ++c)
        p_InfoForward.ch_names.push_back(p_InfoForward.chs[c].ch_name);

    p_InfoForward.nchan = chs.size();

    //
    //   Get the MEG device <-> head coordinate transformation
    //
    if (t_pTag = parent_meg[0]->find_tag(*this, FIFF_COORD_TRANS); t_pTag) {
        cand = t_pTag->toCoordTrans();
        if (cand.from == FIFFV_COORD_DEVICE && cand.to == FIFFV_COORD_HEAD)
            p_InfoForward.dev_head_t = cand;
        else if (cand.from == FIFFV_MNE_COORD_CTF_HEAD && cand.to == FIFFV_COORD_HEAD)
            p_InfoForward.ctf_head_t = cand;
        else
            qWarning("MEG device/head coordinate transformation not found");
    } else
        qWarning("MEG/head coordinate transformation not found.\n");

    //
    //   Load the bad channel list
    //
    p_InfoForward.bads = this->read_bad_channels(p_Node);

    return true;
}

//=============================================================================================================

bool FiffStream::read_meas_info(const FiffDirNode::SPtr& p_Node, FiffInfo& info, FiffDirNode::SPtr& p_NodeInfo)
{
    //    if (info)
    //        delete info;
    info.clear();
    //
    //   Find the desired blocks
    //
    std::vector<FiffDirNode::SPtr> meas = p_Node->dir_tree_find(FIFFB_MEAS);

    if (meas.size() == 0) {
        qWarning("Could not find measurement data\n");
        return false;
    }
    //
    std::vector<FiffDirNode::SPtr> meas_info = meas[0]->dir_tree_find(FIFFB_MEAS_INFO);
    if (meas_info.size() == 0) {
        qWarning("Could not find measurement info\n");
        //        delete meas[0];
        return false;
    }
    //
    //   Read measurement info
    //
    std::optional<FiffTag> t_pTag;

    fiff_int_t nchan = -1;
    float sfreq = -1.0f;
    float linefreq = -1.0f;
    QList<FiffChInfo> chs;
    float lowpass = -1.0f;
    float highpass = -1.0f;

    int proj_id = 0;
    std::string proj_name;
    std::string xplotter_layout;

    std::string utc_offset;
    fiff_int_t gantry_angle = -1;

    std::string experimenter;
    std::string description;

    FiffChInfo t_chInfo;

    FiffCoordTrans cand;       // = nullptr;
    FiffCoordTrans dev_head_t; // = nullptr;
    FiffCoordTrans ctf_head_t; // = nullptr;
    QList<FiffCoordTrans> all_coord_trans;

    fiff_int_t meas_date[2];
    meas_date[0] = -1;
    meas_date[1] = -1;

    fiff_int_t kind = -1;
    fiff_int_t pos = -1;

    for (qint32 k = 0; k < meas_info[0]->nent(); ++k) {
        kind = meas_info[0]->dir[k].kind;
        pos = meas_info[0]->dir[k].pos;
        switch (kind) {
            case FIFF_NCHAN:
                t_pTag = this->read_tag(pos);
                nchan = *t_pTag->toInt();
                break;
            case FIFF_SFREQ:
                t_pTag = this->read_tag(pos);
                sfreq = *t_pTag->toFloat();
                break;
            case FIFF_LINE_FREQ:
                t_pTag = this->read_tag(pos);
                linefreq = *t_pTag->toFloat();
                break;
            case FIFF_CH_INFO:
                t_pTag = this->read_tag(pos);
                chs.append(t_pTag->toChInfo());
                break;
            case FIFF_LOWPASS:
                t_pTag = this->read_tag(pos);
                lowpass = *t_pTag->toFloat();
                break;
            case FIFF_HIGHPASS:
                t_pTag = this->read_tag(pos);
                highpass = *t_pTag->toFloat();
                break;
            case FIFF_MEAS_DATE:
                t_pTag = this->read_tag(pos);
                meas_date[0] = t_pTag->toInt()[0];
                meas_date[1] = t_pTag->toInt()[1];
                break;
            case FIFF_COORD_TRANS:
                //ToDo: This has to be debugged!!
                t_pTag = this->read_tag(pos);
                cand = t_pTag->toCoordTrans();
                all_coord_trans.append(cand);
                if (cand.from == FIFFV_COORD_DEVICE && cand.to == FIFFV_COORD_HEAD)
                    dev_head_t = cand;
                else if (cand.from == FIFFV_MNE_COORD_CTF_HEAD && cand.to == FIFFV_COORD_HEAD)
                    ctf_head_t = cand;
                break;
            case FIFF_PROJ_ID:
                t_pTag = this->read_tag(pos);
                proj_id = *t_pTag->toInt();
                break;
            case FIFF_PROJ_NAME:
                t_pTag = this->read_tag(pos);
                proj_name = t_pTag->toString();
                break;
            case FIFF_XPLOTTER_LAYOUT:
                t_pTag = this->read_tag(pos);
                xplotter_layout = t_pTag->toString();
                break;
            case FIFF_EXPERIMENTER:
                t_pTag = this->read_tag(pos);
                experimenter = t_pTag->toString();
                break;
            case FIFF_DESCRIPTION:
                t_pTag = this->read_tag(pos);
                description = t_pTag->toString();
                break;
            case FIFF_GANTRY_ANGLE:
                t_pTag = this->read_tag(pos);
                // MNE-Python writes this as float; older writers used int.
                if (t_pTag->getType() == FIFFT_FLOAT) {
                    gantry_angle = static_cast<fiff_int_t>(*t_pTag->toFloat());
                } else if (t_pTag->getType() == FIFFT_INT) {
                    gantry_angle = *t_pTag->toInt();
                }
                break;
            case FIFF_UTC_OFFSET:
                t_pTag = this->read_tag(pos);
                utc_offset = t_pTag->toString();
                break;
        }
    }
    //
    //   Check that we have everything we need
    //
    if (nchan < 0) {
        qWarning("Number of channels in not defined\n");
        return false;
    }
    if (sfreq < 0) {
        qWarning("Sampling frequency is not defined\n");
        return false;
    }
    if (linefreq < 0) {
        qWarning("Line frequency is not defined\n");
    }
    if (chs.size() == 0) {
        qWarning("Channel information not defined\n");
        return false;
    }
    if (chs.size() != nchan) {
        qWarning("Incorrect number of channel definitions found\n");
        return false;
    }

    if (dev_head_t.is_empty() || ctf_head_t.is_empty()) {
        std::vector<FiffDirNode::SPtr> hpi_result = meas_info[0]->dir_tree_find(FIFFB_HPI_RESULT);
        if (hpi_result.size() == 1) {
            for (qint32 k = 0; k < hpi_result[0]->nent(); ++k) {
                kind = hpi_result[0]->dir[k].kind;
                pos = hpi_result[0]->dir[k].pos;
                if (kind == FIFF_COORD_TRANS) {
                    t_pTag = this->read_tag(pos);
                    cand = t_pTag->toCoordTrans();
                    if (cand.from == FIFFV_COORD_DEVICE && cand.to == FIFFV_COORD_HEAD)
                        dev_head_t = cand;
                    else if (cand.from == FIFFV_MNE_COORD_CTF_HEAD && cand.to == FIFFV_COORD_HEAD)
                        ctf_head_t = cand;
                }
            }
        }
    }
    //
    //   Locate the Polhemus data
    //
    std::vector<FiffDirNode::SPtr> isotrak = meas_info[0]->dir_tree_find(FIFFB_ISOTRAK);

    QList<FiffDigPoint> dig;
    fiff_int_t coord_frame = FIFFV_COORD_HEAD;
    FiffCoordTrans dig_trans;
    qint32 k = 0;

    if (isotrak.size() == 1) {
        for (k = 0; k < isotrak[0]->nent(); ++k) {
            kind = isotrak[0]->dir[k].kind;
            pos = isotrak[0]->dir[k].pos;
            if (kind == FIFF_DIG_POINT) {
                t_pTag = this->read_tag(pos);
                dig.append(t_pTag->toDigPoint());
            } else {
                if (kind == FIFF_MNE_COORD_FRAME) {
                    t_pTag = this->read_tag(pos);
                    coord_frame = *t_pTag->toInt();
                } else if (kind == FIFF_COORD_TRANS) {
                    t_pTag = this->read_tag(pos);
                    dig_trans = t_pTag->toCoordTrans();
                }
            }
        }
    }
    for (k = 0; k < dig.size(); ++k)
        dig[k].coord_frame = coord_frame;

    if (!dig_trans.is_empty()) //if exist('dig_trans','var')
        if (dig_trans.from != coord_frame && dig_trans.to != coord_frame)
            dig_trans.clear();

    //
    //   Locate the acquisition information
    //
    std::vector<FiffDirNode::SPtr> acqpars = meas_info[0]->dir_tree_find(FIFFB_DACQ_PARS);
    std::string acq_pars;
    std::string acq_stim;
    if (acqpars.size() == 1) {
        for (k = 0; k < acqpars[0]->nent(); ++k) {
            kind = acqpars[0]->dir[k].kind;
            pos = acqpars[0]->dir[k].pos;
            if (kind == FIFF_DACQ_PARS) {
                t_pTag = this->read_tag(pos);
                acq_pars = t_pTag->toString();
            } else if (kind == FIFF_DACQ_STIM) {
                t_pTag = this->read_tag(pos);
                acq_stim = t_pTag->toString();
            }
        }
    }
    std::vector<FiffDirNode::SPtr> device = meas_info[0]->dir_tree_find(FIFFB_DEVICE);
    FiffDeviceInfo device_info;
    if (device.size() == 1) {
        for (k = 0; k < device[0]->nent(); ++k) {
            kind = device[0]->dir[k].kind;
            std::string* field = kind == FIFF_DEVICE_TYPE ? &device_info.type
                : kind == FIFF_DEVICE_MODEL               ? &device_info.model
                : kind == FIFF_DEVICE_SERIAL              ? &device_info.serial
                : kind == FIFF_DEVICE_SITE                ? &device_info.site
                                                          : nullptr;
            if (field == nullptr) {
                continue;
            }
            if (const auto tag = this->read_tag(device[0]->dir[k].pos)) {
                *field = tag->toString();
            }
        }
    }
    //
    //   Load the SSP data
    //
    QList<FiffProj> projs = this->read_proj(meas_info[0]); //ToDo Member Function
    //
    //   Load the CTF compensation data
    //
    QList<FiffCtfComp> comps = this->read_ctf_comp(meas_info[0], chs); //ToDo Member Function
    //
    //   Load the bad channel list
    //
    StringList bads = this->read_bad_channels(p_Node);
    //
    //   Put the data together
    //
    //    info = new FiffInfo();
    if (p_Node->id.version != -1)
        info.file_id = p_Node->id;
    else
        info.file_id.version = -1;

    //
    //  Make the most appropriate selection for the measurement id
    //
    if (meas_info[0]->parent_id.version == -1) {
        if (meas_info[0]->id.version == -1) {
            if (meas[0]->id.version == -1) {
                if (meas[0]->parent_id.version == -1)
                    info.meas_id = info.file_id;
                else
                    info.meas_id = meas[0]->parent_id;
            } else
                info.meas_id = meas[0]->id;
        } else
            info.meas_id = meas_info[0]->id;
    } else
        info.meas_id = meas_info[0]->parent_id;

    if (meas_date[0] == -1) {
        info.meas_date[0] = info.meas_id.time.secs;
        info.meas_date[1] = info.meas_id.time.usecs;
    } else {
        info.meas_date[0] = meas_date[0];
        info.meas_date[1] = meas_date[1];
    }

    info.nchan = nchan;
    info.sfreq = sfreq;
    info.linefreq = linefreq;

    if (highpass != -1.0f)
        info.highpass = highpass;
    else
        info.highpass = 0.0f;

    if (lowpass != -1.0f)
        info.lowpass = lowpass;
    else
        info.lowpass = info.sfreq / 2.0;

    //
    //   Add the channel information and make a list of channel names
    //   for convenience
    //
    info.chs = chs;
    for (qint32 c = 0; c < info.nchan; ++c)
        info.ch_names.push_back(info.chs[c].ch_name);

    //
    //  Add the coordinate transformations
    //
    info.dev_head_t = dev_head_t;
    info.ctf_head_t = ctf_head_t;
    info.all_coord_trans = all_coord_trans;
    if (!info.dev_head_t.is_empty() && !info.ctf_head_t.is_empty())
        info.dev_ctf_t = FiffCoordTrans(FIFFV_COORD_DEVICE, FIFFV_MNE_COORD_CTF_HEAD, ctf_head_t.invtrans * dev_head_t.trans);
    else
        info.dev_ctf_t.clear();

    //
    //   All kinds of auxliary stuff
    //
    info.dig = dig;
    if (!dig_trans.is_empty())
        info.dig_trans = dig_trans;

    info.experimenter = experimenter;
    info.description = description;
    info.proj_id = proj_id;
    info.proj_name = proj_name;
    info.xplotter_layout = xplotter_layout;
    info.gantry_angle = gantry_angle;
    info.utc_offset = utc_offset;

    info.bads = bads;
    info.projs = projs;
    info.comps = comps;
    info.acq_pars = acq_pars;
    info.acq_stim = acq_stim;
    info.device_info = device_info;

    //
    //   Locate the HPI coil excitation frequencies
    //
    //   They live in one FIFFB_HPI_COIL block per coil inside the
    //   FIFFB_HPI_MEAS block. Each coil block carries a FIFF_HPI_COIL_FREQ tag.
    //   Files without an HPI measurement simply leave the list empty.
    //
    std::vector<FiffDirNode::SPtr> hpi_meas = meas_info[0]->dir_tree_find(FIFFB_HPI_MEAS);
    if (!hpi_meas.empty()) {
        std::vector<FiffDirNode::SPtr> hpi_coils = hpi_meas[0]->dir_tree_find(FIFFB_HPI_COIL);

        for (const auto& hpi_coil : hpi_coils) {
            for (qint32 e = 0; e < hpi_coil->nent(); ++e) {
                if (hpi_coil->dir[e].kind != FIFF_HPI_COIL_FREQ) {
                    continue;
                }

                t_pTag = this->read_tag(hpi_coil->dir[e].pos);
                info.hpi_coil_freqs.append(*t_pTag->toFloat());
                break;
            }
        }
    }

    p_NodeInfo = meas[0];

    return true;
}

//=============================================================================================================

bool FiffStream::read_named_matrix(const FiffDirNode::SPtr& p_Node, fiff_int_t matkind, FiffNamedMatrix& mat)
{
    mat.clear();

    FiffDirNode::SPtr node = p_Node;
    //
    //   Descend one level if necessary
    //
    bool found_it = false;
    if (node->type != FIFFB_MNE_NAMED_MATRIX) {
        for (int k = 0; k < node->nchild(); ++k) {
            if (node->children[k]->type == FIFFB_MNE_NAMED_MATRIX) {
                if (node->children[k]->has_tag(matkind)) {
                    node = node->children[k];
                    found_it = true;
                    break;
                }
            }
        }
        if (!found_it) {
            qWarning("Fiff::read_named_matrix: Desired named matrix (kind = %d) not available\n", matkind);
            return false;
        }
    } else {
        if (!node->has_tag(matkind)) {
            qWarning("Desired named matrix (kind = %d) not available", matkind);
            return false;
        }
    }

    std::optional<FiffTag> t_pTag;
    //
    //   Read everything we need
    //
    if (t_pTag = node->find_tag(*this, matkind); !t_pTag) {
        qWarning("Matrix data missing.\n");
        return false;
    } else {
        //qDebug() << "Is Matrix" << t_pTag->isMatrix() << "Special Type:" << t_pTag->getType();
        mat.data = t_pTag->toFloatMatrix().cast<double>();
        mat.data.transposeInPlace();
    }

    mat.nrow = mat.data.rows();
    mat.ncol = mat.data.cols();

    if (t_pTag = node->find_tag(*this, FIFF_MNE_NROW); t_pTag)
        if (*t_pTag->toInt() != mat.nrow) {
            qWarning("Number of rows in matrix data and FIFF_MNE_NROW tag do not match");
            return false;
        }
    if (t_pTag = node->find_tag(*this, FIFF_MNE_NCOL); t_pTag)
        if (*t_pTag->toInt() != mat.ncol) {
            qWarning("Number of columns in matrix data and FIFF_MNE_NCOL tag do not match");
            return false;
        }

    std::string row_names;
    if (t_pTag = node->find_tag(*this, FIFF_MNE_ROW_NAMES); t_pTag)
        row_names = t_pTag->toString();

    std::string col_names;
    if (t_pTag = node->find_tag(*this, FIFF_MNE_COL_NAMES); t_pTag)
        col_names = t_pTag->toString();

    //
    //   Put it together
    //
    if (!row_names.empty())
        mat.row_names = split_name_list(row_names);

    if (!col_names.empty())
        mat.col_names = split_name_list(col_names);

    if (static_cast<int>(mat.row_names.size()) != mat.nrow) {
        qWarning("FiffStream::read_named_matrix - Number of rows in matrix data and row names do not match\n");
    }

    if (static_cast<int>(mat.col_names.size()) != mat.ncol) {
        qWarning("FiffStream::read_named_matrix - Number of columns in matrix data and column names do not match\n");
    }

    return true;
}

//=============================================================================================================

QList<FiffProj> FiffStream::read_proj(const FiffDirNode::SPtr& p_Node)
{
    QList<FiffProj> projdata; // = struct('kind',{},'active',{},'desc',{},'data',{});
    //
    //   Locate the projection data
    //
    std::vector<FiffDirNode::SPtr> t_qListNodes = p_Node->dir_tree_find(FIFFB_PROJ);
    if (t_qListNodes.size() == 0)
        return projdata;

    std::optional<FiffTag> t_pTag;
    t_pTag = t_qListNodes[0]->find_tag(*this, FIFF_NCHAN);
    fiff_int_t global_nchan = 0;
    if (t_pTag)
        global_nchan = *t_pTag->toInt();

    fiff_int_t nchan;
    std::vector<FiffDirNode::SPtr> t_qListItems = t_qListNodes[0]->dir_tree_find(FIFFB_PROJ_ITEM);
    for (qint32 i = 0; i < std::ssize(t_qListItems); ++i) {
        //
        //   Find all desired tags in one item
        //
        FiffDirNode::SPtr t_pFiffDirTreeItem = t_qListItems[i];
        t_pTag = t_pFiffDirTreeItem->find_tag(*this, FIFF_NCHAN);
        if (t_pTag)
            nchan = *t_pTag->toInt();
        else
            nchan = global_nchan;

        t_pTag = t_pFiffDirTreeItem->find_tag(*this, FIFF_DESCRIPTION);
        QString desc; // maybe, in some cases this has to be a struct.
        if (t_pTag) {
            desc = QString::fromStdString(t_pTag->toString());
        } else {
            t_pTag = t_pFiffDirTreeItem->find_tag(*this, FIFF_NAME);
            if (t_pTag)
                desc = QString::fromStdString(t_pTag->toString());
            else {
                qWarning("Projection item description missing\n");
                return projdata;
            }
        }
        //            (t_pTag = t_pFiffDirTreeItem->find_tag(this, FIFF_PROJ_ITEM_CH_NAME_LIST));
        //            QString namelist;
        //            if (t_pTag)
        //            {
        //                namelist = t_pTag->toString();
        //            }
        //            else
        //            {
        //                qCritical("Projection item channel list missing");
        //                return projdata;
        //            }
        t_pTag = t_pFiffDirTreeItem->find_tag(*this, FIFF_PROJ_ITEM_KIND);
        fiff_int_t kind;
        if (t_pTag) {
            kind = *t_pTag->toInt();
        } else {
            qWarning("Projection item kind missing");
            return projdata;
        }
        t_pTag = t_pFiffDirTreeItem->find_tag(*this, FIFF_PROJ_ITEM_NVEC);
        fiff_int_t nvec;
        if (t_pTag) {
            nvec = *t_pTag->toInt();
        } else {
            qWarning("Number of projection vectors not specified\n");
            return projdata;
        }
        t_pTag = t_pFiffDirTreeItem->find_tag(*this, FIFF_PROJ_ITEM_CH_NAME_LIST);
        StringList names;
        if (t_pTag) {
            names = split_name_list(t_pTag->toString());
        } else {
            qWarning("Projection item channel list missing\n");
            return projdata;
        }
        t_pTag = t_pFiffDirTreeItem->find_tag(*this, FIFF_PROJ_ITEM_VECTORS);
        MatrixXd data; // = nullptr;
        if (t_pTag) {
            data = t_pTag->toFloatMatrix().cast<double>();
            data.transposeInPlace();
        } else {
            qWarning("Projection item data missing\n");
            return projdata;
        }
        t_pTag = t_pFiffDirTreeItem->find_tag(*this, FIFF_MNE_PROJ_ITEM_ACTIVE);
        bool active;
        if (t_pTag)
            active = *t_pTag->toInt();
        else
            active = false;

        if (data.cols() != static_cast<int>(names.size())) {
            qWarning("Number of channel names does not match the size of data matrix\n");
            return projdata;
        }

        //
        //   create a named matrix for the data
        //
        StringList defaultList;
        FiffNamedMatrix t_fiffNamedMatrix(nvec, nchan, defaultList, names, data);

        FiffProj one(kind, active, desc, t_fiffNamedMatrix);
        //
        projdata.append(one);
    }

    if (projdata.size() > 0) {
        qInfo("\tRead a total of %lld projection items:\n", static_cast<long long>(projdata.size()));
        for (qint32 k = 0; k < projdata.size(); ++k) {
            qInfo("\t\t%s (%d x %d) %s\n", projdata[k].desc.toUtf8().constData(), projdata[k].data->nrow, projdata[k].data->ncol, projdata[k].active ? "active" : "idle");
        }
    }

    return projdata;
}

//=============================================================================================================

bool FiffStream::read_tag_data(FiffTag& tag, fiff_long_t pos)
{
    if (pos >= 0) {
        seekTo(pos);
    }
    if (tag.size() > 0) {
        readRaw(tag.data(), tag.size());
        FiffTag::convert_tag_data(tag, FIFFV_BIG_ENDIAN, FIFFV_NATIVE_ENDIAN);
    }
    if (tag.next != FIFFV_NEXT_SEQ) {
        seekTo(tag.next);
    }
    return !std::exchange(m_readFailed, false);
}

//=============================================================================================================

std::optional<FiffTag> FiffStream::read_tag_info(bool skip)
{
    FiffTag tag;
    read(tag.kind);
    read(tag.type);
    qint32 size = 0;
    read(size);
    read(tag.next);
    if (std::exchange(m_readFailed, false)) {
        return std::nullopt;
    }
    tag.resize(size);

    if (skip) {
        if (qobject_cast<QTcpSocket*>(this->device())) {
            skipRaw(tag.size());
        } else if (tag.next > 0) {
            if (!seekTo(tag.next)) {
                return std::nullopt;
            }
        } else if (tag.size() > 0 && tag.next == FIFFV_NEXT_SEQ) {
            if (!seekTo(tell() + tag.size())) {
                return std::nullopt;
            }
        }
    }
    return tag;
}

//=============================================================================================================

std::optional<FiffTag> FiffStream::read_rt_tag()
{
    while (this->device()->bytesAvailable() < 16) {
        this->device()->waitForReadyRead(10);
    }
    auto tag = read_tag_info(false);
    if (!tag) {
        return std::nullopt;
    }
    while (this->device()->bytesAvailable() < tag->size()) {
        this->device()->waitForReadyRead(10);
    }
    if (!read_tag_data(*tag)) {
        return std::nullopt;
    }
    return tag;
}

//=============================================================================================================

std::optional<FiffTag> FiffStream::read_tag(fiff_long_t pos)
{
    if (pos >= 0) {
        seekTo(pos);
    }
    FiffTag tag;
    read(tag.kind);
    read(tag.type);
    qint32 size = 0;
    read(size);
    read(tag.next);
    if (std::exchange(m_readFailed, false) || size < 0) {
        return std::nullopt;
    }
    tag.resize(size);
    if (size > 0) {
        readRaw(tag.data(), size);
        FiffTag::convert_tag_data(tag, m_littleEndian ? FIFFV_LITTLE_ENDIAN : FIFFV_BIG_ENDIAN, FIFFV_NATIVE_ENDIAN);
    }
    if (tag.next != FIFFV_NEXT_SEQ) {
        seekTo(tag.next);
    }
    if (std::exchange(m_readFailed, false)) {
        return std::nullopt;
    }
    return tag;
}

//=============================================================================================================

bool FiffStream::setup_read_raw(const std::filesystem::path& path,
                                FiffRawData& data,
                                bool allow_maxshield,
                                bool is_littleEndian)
{
    auto bytes = UTILSLIB::FileByteStream::open(path, UTILSLIB::FileByteStream::Mode::Read);
    if (!bytes) {
        qCritical("Cannot open %s", UTILSLIB::utf8FromPath(path).c_str());
        return false;
    }
    return setup_read_raw(std::make_unique<FiffStream>(std::move(bytes), UTILSLIB::utf8FromPath(path)), data, allow_maxshield, is_littleEndian);
}

//=============================================================================================================

bool FiffStream::setup_read_raw(std::unique_ptr<UTILSLIB::ByteStream> bytes,
                                FiffRawData& data,
                                bool allow_maxshield,
                                bool is_littleEndian)
{
    if (!bytes)
        return false;
    return setup_read_raw(std::make_unique<FiffStream>(std::move(bytes)), data, allow_maxshield, is_littleEndian);
}

//=============================================================================================================

bool FiffStream::setup_read_raw(std::unique_ptr<FiffStream> t_pStream,
                                FiffRawData& data,
                                bool allow_maxshield,
                                bool is_littleEndian)
{
    QString t_sFileName = t_pStream->streamName();

    if (is_littleEndian) {
        //qInfo("Setting stream to little Endian");
        t_pStream->setLittleEndian(true);
    }

    qInfo("Opening raw data %s...\n", t_sFileName.toUtf8().constData());

    if (!t_pStream->open()) {
        return false;
    }

    //
    //   Read the measurement info
    //
    FiffInfo info; // = nullptr;
    FiffDirNode::SPtr meas;
    if (!t_pStream->read_meas_info(t_pStream->dirtree(), info, meas))
        return false;

    //
    //   Locate the data of interest
    //
    std::vector<FiffDirNode::SPtr> raw = meas->dir_tree_find(FIFFB_RAW_DATA);
    if (raw.size() == 0) {
        raw = meas->dir_tree_find(FIFFB_CONTINUOUS_DATA);
        if (allow_maxshield) {
            //            for (qint32 i = 0; i < raw.size(); ++i)
            //                if(raw[i])
            //                    delete raw[i];
            raw = meas->dir_tree_find(FIFFB_SMSH_RAW_DATA);
            qInfo("Maxshield data found\n");

            if (raw.size() == 0) {
                qWarning("No raw data in %s\n", t_sFileName.toUtf8().constData());
                return false;
            }
        } else {
            if (raw.size() == 0) {
                qWarning("No raw data in %s\n", t_sFileName.toUtf8().constData());
                return false;
            }
        }
    }
    //
    //   Set up the output structure
    //
    info.filename = t_sFileName.toStdString();

    data.clear();
    data.file = std::move(t_pStream);
    data.info = info;
    data.first_samp = 0;
    data.last_samp = 0;
    //
    //   Process the directory
    //
    std::vector<FiffDirEntry> dir = raw[0]->dir;
    fiff_int_t nent = raw[0]->nent();
    fiff_int_t nchan = info.nchan;
    fiff_int_t first = 0;
    fiff_int_t first_samp = 0;
    fiff_int_t first_skip = 0;
    //
    //  A malformed file may have a raw data block with an empty directory.
    //  Bail out cleanly instead of indexing past the end of dir below.
    //
    if (dir.empty()) {
        qWarning("No directory entries in raw data block of %s\n", t_sFileName.toUtf8().constData());
        return false;
    }
    //
    //  The per-buffer sample count below is derived by dividing the data
    //  buffer size by the channel count. A malformed measurement info with no
    //  channels would cause a division by zero, so reject it up front.
    //
    if (nchan <= 0) {
        qWarning("Measurement info of %s reports %d channels; cannot read raw data\n", t_sFileName.toUtf8().constData(), nchan);
        return false;
    }
    //
    //  Get first sample tag if it is there
    //
    std::optional<FiffTag> t_pTag;
    if (dir[first].kind == FIFF_FIRST_SAMPLE) {
        t_pTag = data.file->read_tag(dir[first].pos);
        first_samp = *t_pTag->toInt();
        ++first;
    }
    //
    //  Omit initial skip
    //
    if (first < std::ssize(dir) && dir[first].kind == FIFF_DATA_SKIP) {
        //
        //  This first skip can be applied only after we know the buffer size
        //
        t_pTag = data.file->read_tag(dir[first].pos);
        first_skip = *t_pTag->toInt();
        ++first;
    }
    data.first_samp = first_samp;
    //
    //   Go through the remaining tags in the directory
    //
    QList<FiffRawDir> rawdir;
    //        rawdir = struct('ent',{},'first',{},'last',{},'nsamp',{});
    fiff_int_t nskip = 0;
    fiff_int_t nsamp = 0;
    for (qint32 k = first; k < nent; ++k) {
        FiffDirEntry ent = dir[k];
        if (ent.kind == FIFF_DATA_SKIP) {
            t_pTag = data.file->read_tag(ent.pos);
            nskip = *t_pTag->toInt();
        } else if (ent.kind == FIFF_DATA_BUFFER) {
            //
            //   Figure out the number of samples in this buffer
            //
            switch (ent.type) {
                case FIFFT_DAU_PACK16:
                    nsamp = ent.size / (2 * nchan);
                    break;
                case FIFFT_SHORT:
                    nsamp = ent.size / (2 * nchan);
                    break;
                case FIFFT_FLOAT:
                    nsamp = ent.size / (4 * nchan);
                    break;
                case FIFFT_INT:
                    nsamp = ent.size / (4 * nchan);
                    break;
                case FIFFT_DOUBLE:
                    nsamp = ent.size / (8 * nchan);
                    break;
                default:
                    qWarning("Cannot handle data buffers of type %d\n", ent.type);
                    return false;
            }
            //
            //  Do we have an initial skip pending?
            //
            if (first_skip > 0) {
                first_samp += nsamp * first_skip;
                data.first_samp = first_samp;
                first_skip = 0;
            }
            //
            //  Do we have a skip pending?
            //
            if (nskip > 0) {
                FiffRawDir t_RawDir;
                // Readers recognise a skip by ent.kind == -1 (the FiffDirEntry default) and fill it with zeros.
                t_RawDir.ent = FiffDirEntry{};
                t_RawDir.first = first_samp;
                t_RawDir.last = first_samp + nskip * nsamp - 1; //ToDo -1 right or is that MATLAB syntax
                t_RawDir.nsamp = nskip * nsamp;
                rawdir.append(t_RawDir);
                first_samp = first_samp + nskip * nsamp;
                nskip = 0;
            }
            //
            //  Add a data buffer
            //
            FiffRawDir t_RawDir;
            t_RawDir.ent = ent;
            t_RawDir.first = first_samp;
            t_RawDir.last = first_samp + nsamp - 1; //ToDo -1 right or is that MATLAB syntax
            t_RawDir.nsamp = nsamp;
            rawdir.append(t_RawDir);
            first_samp += nsamp;
        }
    }
    data.last_samp = first_samp - 1; //ToDo -1 right or is that MATLAB syntax
    //
    //   Add the calibration factors
    //
    RowVectorXd cals(data.info.nchan);
    cals.setZero();
    for (qint32 k = 0; k < data.info.nchan; ++k)
        cals[k] = static_cast<double>(data.info.chs[k].range) * data.info.chs[k].cal;
    //
    data.cals = cals;
    data.rawdir = rawdir;
    //data->proj       = [];
    //data.comp       = [];
    //
    qInfo("\tRange : %d ... %d  =  %9.3f ... %9.3f secs",
          data.first_samp, data.last_samp,
          static_cast<double>(data.first_samp) / data.info.sfreq,
          static_cast<double>(data.last_samp) / data.info.sfreq);
    qInfo("Ready.");
    data.file->close();

    return true;
}

//=============================================================================================================

StringList FiffStream::split_name_list(std::string_view p_sNameList)
{
    // Spaces are dropped from the whole list before splitting, as MNE-C does
    std::string compact;
    compact.reserve(p_sNameList.size());
    for (const char c : p_sNameList) {
        if (c != ' ')
            compact.push_back(c);
    }
    return UTILSLIB::split(compact, ':');
}

//=============================================================================================================

fiff_long_t FiffStream::start_block(fiff_int_t kind)
{
    return this->write_int(FIFF_BLOCK_START, &kind);
}

//=============================================================================================================

std::unique_ptr<FiffStream> FiffStream::start_file(QIODevice& p_IODevice)
{
    auto p_pStream = std::make_unique<FiffStream>(&p_IODevice);
    QString t_sFileName = p_pStream->streamName();

    if (!p_IODevice.isOpen()) {
        if (!p_pStream->device()->open(QIODevice::WriteOnly)) {
            qWarning("Cannot write to %s\n", t_sFileName.toUtf8().constData()); //consider throw
            std::unique_ptr<FiffStream> p_pEmptyStream;
            return p_pEmptyStream;
        }
    }

    //
    //   Write the compulsory items
    //
    p_pStream->write_id(FIFF_FILE_ID); //1
    int null_pointer = FIFFV_NEXT_NONE;
    p_pStream->write_int(FIFF_DIR_POINTER, &null_pointer); //2
    p_pStream->write_int(FIFF_FREE_LIST, &null_pointer);   //3
    //
    //   Ready for more
    //
    return p_pStream;
}

//=============================================================================================================

std::unique_ptr<FiffStream> FiffStream::open_update(QIODevice& p_IODevice)
{
    auto t_pStream = std::make_unique<FiffStream>(&p_IODevice);
    QString t_sFileName = t_pStream->streamName();

    /*
     * ReadWrite creates the file, so a path that is not there would be turned into
     * an empty file that then fails to parse, leaving the caller's directory dirty.
     * Updating a file that does not exist is never the intent.
     */
    QFile* t_pFile = qobject_cast<QFile*>(&p_IODevice);
    if (t_pFile != nullptr && !t_pFile->exists()) {
        qCritical("Cannot open %s for update: no such file", t_sFileName.toUtf8().constData());
        return std::unique_ptr<FiffStream>();
    }

    /*
     * Try to open...
     */
    if (!t_pStream->open(QIODevice::ReadWrite)) {
        qCritical("Cannot open %s\n", t_sFileName.toUtf8().constData()); //consider throw
        return std::unique_ptr<FiffStream>();
    }

    std::optional<FiffTag> t_pTag;
    long dirpos, pointerpos;

    QFile* file = qobject_cast<QFile*>(t_pStream->device());

    if (file != nullptr) {
        /*
        * Ensure that the last tag in the directory has next set to FIFF_NEXT_NONE
        */
        pointerpos = t_pStream->dir()[t_pStream->nent() - 2].pos;
        if (t_pTag = t_pStream->read_tag(pointerpos); !t_pTag) {
            qCritical("Could not read last tag in the directory list!");
            t_pStream->close();
            return std::unique_ptr<FiffStream>();
        }
        if (t_pTag->next != FIFFV_NEXT_NONE) {
            t_pTag->next = FIFFV_NEXT_NONE;
            t_pStream->write_tag(*t_pTag, pointerpos);
        }
        /*
        * Read directory pointer
        */
        pointerpos = t_pStream->dir()[1].pos;
        if (t_pTag = t_pStream->read_tag(pointerpos); !t_pTag) {
            qCritical("Could not read directory pointer!");
            t_pStream->close();
            return std::unique_ptr<FiffStream>();
        }
        /*
        * Do we have a directory?
        */
        dirpos = *t_pTag->toInt();
        if (dirpos > 0) {
            /*
            * Yes! We will ignore it.
            */
            t_pStream->write_dir_pointer(-1, pointerpos);
            /*
            * Clean up the trailing end
            */
            file->resize(dirpos); //truncate file to new size
        }
        /*
        * Seek to end for writing
        */
        t_pStream->device()->seek(file->size()); //SEEK_END
    }
    return t_pStream;
}

//=============================================================================================================

std::unique_ptr<FiffStream> FiffStream::start_writing_raw(const std::filesystem::path& path,
                                                          const FiffInfo& info,
                                                          RowVectorXd& cals,
                                                          MatrixXi sel,
                                                          bool bResetRange,
                                                          fiff_int_t dataType)
{
    return start_writing_raw(start_file(path), info, cals, std::move(sel), bResetRange, dataType);
}

//=============================================================================================================

std::unique_ptr<FiffStream> FiffStream::start_writing_raw(std::unique_ptr<UTILSLIB::ByteStream> bytes,
                                                          const FiffInfo& info,
                                                          RowVectorXd& cals,
                                                          MatrixXi sel,
                                                          bool bResetRange,
                                                          fiff_int_t dataType)
{
    if (!bytes)
        return {};
    return start_writing_raw(start_file(std::make_unique<FiffStream>(std::move(bytes))), info, cals, std::move(sel), bResetRange, dataType);
}

//=============================================================================================================

std::unique_ptr<FiffStream> FiffStream::start_writing_raw(std::unique_ptr<FiffStream> t_pStream,
                                                          const FiffInfo& info,
                                                          RowVectorXd& cals,
                                                          MatrixXi sel,
                                                          bool bResetRange,
                                                          fiff_int_t dataType)
{
    fiff_int_t data_type = dataType;

    // The channel list is authoritative: nchan is -1 until a reader sets it
    if (sel.cols() == 0) {
        sel.resize(1, info.chs.size());
        for (qint32 k = 0; k < info.chs.size(); ++k)
            sel(0, k) = k;
    }

    QList<FiffChInfo> chs;

    for (qint32 k = 0; k < sel.cols(); ++k)
        chs << info.chs.at(sel(0, k));

    fiff_int_t nchan = chs.size();

    if (!t_pStream) {
        return t_pStream;
    }
    t_pStream->start_block(FIFFB_MEAS); //4
    t_pStream->write_id(FIFF_BLOCK_ID); //5
    if (info.meas_id.version != -1) {
        t_pStream->write_id(FIFF_PARENT_BLOCK_ID, info.meas_id); //6
    }
    //
    //
    //    Measurement info
    //
    t_pStream->start_block(FIFFB_MEAS_INFO); //7

#ifndef WASMBUILD
    //
    //    Blocks from the original
    //
    QList<fiff_int_t> blocks;
    blocks << FIFFB_SUBJECT << FIFFB_HPI_MEAS << FIFFB_HPI_RESULT << FIFFB_HPI_SUBSYSTEM << FIFFB_ISOTRAK << FIFFB_PROCESSING_HISTORY << FIFFB_DACQ_PARS << FIFFB_EVENTS;
    bool have_hpi_result = false;
    bool have_isotrak = false;
    if (blocks.size() > 0 && !info.filename.empty()) {
        QFile t_qFile(QString::fromStdString(info.filename)); //ToDo this has to be adapted for TCPSocket
        FiffStream source(&t_qFile);

        if (!source.open()) {
            qWarning() << "Cannot copy the measurement blocks of" << info.filename << "; writing the info without them";
            blocks.clear();
        }

        for (const fiff_int_t block : blocks) {
            std::vector<FiffDirNode::SPtr> nodes = source.dirtree()->dir_tree_find(block);
            FiffDirNode::copy_tree(source, source.dirtree()->id, nodes, *t_pStream);
            if (block == FIFFB_HPI_RESULT && nodes.size() > 0)
                have_hpi_result = true;

            if (block == FIFFB_ISOTRAK && nodes.size() > 0)
                have_isotrak = true;
        }
    }
#endif

    //
    //    megacq parameters
    //
    if (!info.acq_pars.empty() || !info.acq_stim.empty()) {
        t_pStream->start_block(FIFFB_DACQ_PARS);
        if (!info.acq_pars.empty())
            t_pStream->write_string(FIFF_DACQ_PARS, info.acq_pars);

        if (!info.acq_stim.empty())
            t_pStream->write_string(FIFF_DACQ_STIM, info.acq_stim);

        t_pStream->end_block(FIFFB_DACQ_PARS);
    }
    t_pStream->write_device_info(info.device_info);

#ifndef WASMBUILD
    //
    //    Coordinate transformations if the HPI result block was not there
    //
    if (!have_hpi_result) {
        if (!info.dev_head_t.is_empty())
            t_pStream->write_coord_trans(info.dev_head_t);

        if (!info.ctf_head_t.is_empty())
            t_pStream->write_coord_trans(info.ctf_head_t);
    }
    //
    //    Polhemus data
    //
    if (info.dig.size() > 0 && !have_isotrak) {
        t_pStream->start_block(FIFFB_ISOTRAK);
        for (qint32 k = 0; k < info.dig.size(); ++k)
            t_pStream->write_dig_point(info.dig[k]);

        t_pStream->end_block(FIFFB_ISOTRAK);
    }
#endif

    //
    //    Projectors
    //
    t_pStream->write_proj(info.projs);
    //
    //    CTF compensation info
    //
    t_pStream->write_ctf_comp(info.comps);
    //
    //    Bad channels
    //
    if (info.bads.size() > 0) {
        t_pStream->start_block(FIFFB_MNE_BAD_CHANNELS);
        t_pStream->write_name_list(FIFF_MNE_CH_NAME_LIST, info.bads);
        t_pStream->end_block(FIFFB_MNE_BAD_CHANNELS);
    }
    //
    //    General
    //
    t_pStream->write_float(FIFF_SFREQ, &info.sfreq);
    t_pStream->write_float(FIFF_HIGHPASS, &info.highpass);
    t_pStream->write_float(FIFF_LOWPASS, &info.lowpass);
    if (info.linefreq >= 0.0f)
        t_pStream->write_float(FIFF_LINE_FREQ, &info.linefreq);
    t_pStream->write_int(FIFF_GANTRY_ANGLE, &info.gantry_angle);
    t_pStream->write_int(FIFF_NCHAN, &nchan);
    t_pStream->write_int(FIFF_DATA_PACK, &data_type);
    t_pStream->write_int(FIFF_PROJ_ID, &info.proj_id);
    t_pStream->write_string(FIFF_EXPERIMENTER, info.experimenter);
    t_pStream->write_string(FIFF_DESCRIPTION, info.description);
    t_pStream->write_string(FIFF_PROJ_NAME, info.proj_name);
    t_pStream->write_string(FIFF_XPLOTTER_LAYOUT, info.xplotter_layout);
    if (info.meas_date[0] != -1)
        t_pStream->write_int(FIFF_MEAS_DATE, info.meas_date, 2);
    //
    //    Channel info
    //
    cals = RowVectorXd(nchan);
    for (qint32 k = 0; k < nchan; ++k) {
        //
        //    Scan numbers may have been messed up
        //
        chs[k].scanNo = k + 1;
        if (bResetRange) {
            chs[k].range = 1.0; // Reset to 1.0 because we always write floats.
        }
        cals[k] = chs[k].cal;
        t_pStream->write_ch_info(chs[k]);
    }
    //
    //
    t_pStream->end_block(FIFFB_MEAS_INFO);
    //
    // Start the raw data
    //
    t_pStream->start_block(FIFFB_RAW_DATA);

    return t_pStream;
}

//=============================================================================================================

fiff_long_t FiffStream::write_tag(const FiffTag& tag, fiff_long_t pos)
{
    if (pos >= 0) {
        seekTo(pos);
    } else {
        seekToEnd();
    }
    pos = tell();

    const auto datasize = static_cast<fiff_int_t>(tag.size());
    write(static_cast<qint32>(tag.kind));
    write(static_cast<qint32>(tag.type));
    write(static_cast<qint32>(datasize));
    write(static_cast<qint32>(tag.next));

    if (datasize > 0) {
        // read_tag hands out host byte order; FIFF files are big-endian.
        FiffTag fileTag(tag);
        FiffTag::convert_tag_data(fileTag, FIFFV_NATIVE_ENDIAN, FIFFV_BIG_ENDIAN);
        writeRaw(fileTag.data(), datasize);
    }
    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_ch_info(const FiffChInfo& ch)
{
    fiff_long_t pos = tell();

    //typedef struct _fiffChPosRec {
    //  fiff_int_t   coil_type;          /*!< What kind of coil. */
    //  fiff_float_t r0[3];              /*!< Coil coordinate system origin */
    //  fiff_float_t ex[3];              /*!< Coil coordinate system x-axis unit vector */
    //  fiff_float_t ey[3];              /*!< Coil coordinate system y-axis unit vector */
    //  fiff_float_t ez[3];             /*!< Coil coordinate system z-axis unit vector */
    //} fiffChPosRec,*fiffChPos;        /*!< Measurement channel position and coil type */

    //typedef struct _fiffChInfoRec {
    //  fiff_int_t    scanNo;        /*!< Scanning order # */
    //  fiff_int_t    logNo;         /*!< Logical channel # */
    //  fiff_int_t    kind;          /*!< Kind of channel */
    //  fiff_float_t  range;         /*!< Voltmeter range (only applies to raw data ) */
    //  fiff_float_t  cal;           /*!< Calibration from volts to... */
    //  fiff_ch_pos_t chpos;         /*!< Channel location */
    //  fiff_int_t    unit;          /*!< Unit of measurement */
    //  fiff_int_t    unit_mul;      /*!< Unit multiplier exponent */
    //  fiff_char_t   ch_name[16];   /*!< Descriptive name for the channel */
    //} fiffChInfoRec,*fiffChInfo;   /*!< Description of one channel */
    fiff_int_t datasize = 4 * 13 + 4 * 7 + 16;

    write(static_cast<qint32>(FIFF_CH_INFO));
    write(static_cast<qint32>(FIFFT_CH_INFO_STRUCT));
    write(static_cast<qint32>(datasize));
    write(static_cast<qint32>(FIFFV_NEXT_SEQ));

    //
    //   Start writing fiffChInfoRec
    //
    write(static_cast<qint32>(ch.scanNo));
    write(static_cast<qint32>(ch.logNo));
    write(static_cast<qint32>(ch.kind));

    write(ch.range);
    write(ch.cal);

    //
    //   FiffChPos follows
    //
    write_ch_pos(ch.chpos);

    //
    //   unit and unit multiplier
    //
    write(static_cast<qint32>(ch.unit));
    write(static_cast<qint32>(ch.unit_mul));

    //
    //   Finally channel name
    //
    fiff_int_t len = static_cast<int>(ch.ch_name.size());
    std::string ch_name;
    if (len > 15)
        ch_name = ch.ch_name.substr(0, 15);
    else
        ch_name = ch.ch_name;

    len = static_cast<int>(ch_name.size());

    writeRaw(ch_name.c_str(), len);

    if (len < 16) {
        const char* chNull = "";
        for (qint32 i = 0; i < 16 - len; ++i)
            writeRaw(chNull, 1);
    }

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_ch_pos(const FiffChPos& chpos)
{
    fiff_long_t pos = tell();

    //
    //   FiffChPos
    //
    write(static_cast<qint32>(chpos.coil_type));

    qint32 i;
    // r0
    for (i = 0; i < 3; ++i)
        write(chpos.r0[i]);
    // ex
    for (i = 0; i < 3; ++i)
        write(chpos.ex[i]);
    // ey
    for (i = 0; i < 3; ++i)
        write(chpos.ey[i]);
    // ez
    for (i = 0; i < 3; ++i)
        write(chpos.ez[i]);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_coord_trans(const FiffCoordTrans& trans)
{
    fiff_long_t pos = tell();

    //?typedef struct _fiffCoordTransRec {
    //  fiff_int_t   from;                   /*!< Source coordinate system. */
    //  fiff_int_t   to;                     /*!< Destination coordinate system. */
    //  fiff_float_t rot[3][3];              /*!< The forward transform (rotation part) */
    //  fiff_float_t move[3];                /*!< The forward transform (translation part) */
    //  fiff_float_t invrot[3][3];           /*!< The inverse transform (rotation part) */
    //  fiff_float_t invmove[3];             /*!< The inverse transform (translation part) */
    //} *fiffCoordTrans, fiffCoordTransRec;  /*!< Coordinate transformation descriptor */
    fiff_int_t datasize = 4 * 2 * 12 + 4 * 2;

    write(static_cast<qint32>(FIFF_COORD_TRANS));
    write(static_cast<qint32>(FIFFT_COORD_TRANS_STRUCT));
    write(static_cast<qint32>(datasize));
    write(static_cast<qint32>(FIFFV_NEXT_SEQ));

    //
    //   Start writing fiffCoordTransRec
    //
    write(static_cast<qint32>(trans.from));
    write(static_cast<qint32>(trans.to));

    //
    //   The transform...
    //
    qint32 r, c;
    for (r = 0; r < 3; ++r)
        for (c = 0; c < 3; ++c)
            write(static_cast<float>(trans.trans(r, c)));
    for (r = 0; r < 3; ++r)
        write(static_cast<float>(trans.trans(r, 3)));

    //
    //   ...and its inverse
    //
    for (r = 0; r < 3; ++r)
        for (c = 0; c < 3; ++c)
            write(static_cast<float>(trans.invtrans(r, c)));
    for (r = 0; r < 3; ++r)
        write(static_cast<float>(trans.invtrans(r, 3)));

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_cov(const FiffCov& p_FiffCov)
{
    fiff_long_t pos = tell();

    this->start_block(FIFFB_MNE_COV);

    //
    //   Dimensions etc.
    //
    this->write_int(FIFF_MNE_COV_KIND, &p_FiffCov.kind);
    this->write_int(FIFF_MNE_COV_DIM, &p_FiffCov.dim);
    if (p_FiffCov.nfree > 0)
        this->write_int(FIFF_MNE_COV_NFREE, &p_FiffCov.nfree);
    //
    //   Channel names
    //
    if (p_FiffCov.names.size() > 0)
        this->write_name_list(FIFF_MNE_ROW_NAMES, p_FiffCov.names);
    //
    //   Data
    //
    if (p_FiffCov.diag)
        this->write_double(FIFF_MNE_COV_DIAG, p_FiffCov.data.col(0).data(), p_FiffCov.data.rows());
    else {
        //        if issparse(cov.data)
        //           fiff_write_float_sparse_rcs(fid,FIFF.FIFF_MNE_COV,cov.data);
        //        else
        //        {
        // Store lower triangle including diagonal (matches read_cov format)
        qint32 dim = p_FiffCov.dim;
        qint32 n = dim * (dim + 1) / 2;

        // The loop below writes all n entries, but that is not provable at
        // compile time, so start from a defined state rather than passing a
        // possibly-uninitialised buffer to write_double.
        VectorXd vals = VectorXd::Zero(n);
        qint32 count = 0;
        for (qint32 i = 0; i < dim; ++i)
            for (qint32 j = 0; j <= i; ++j)
                vals(count++) = p_FiffCov.data(i, j);

        this->write_double(FIFF_MNE_COV, vals.data(), vals.size());
        //        }
    }
    //
    //   Eigenvalues and vectors if present
    //
    if (p_FiffCov.eig.size() > 0 && p_FiffCov.eigvec.size() > 0) {
        this->write_float_matrix(FIFF_MNE_COV_EIGENVECTORS, p_FiffCov.eigvec.cast<float>());
        this->write_double(FIFF_MNE_COV_EIGENVALUES, p_FiffCov.eig.data(), p_FiffCov.eig.size());
    }
    //
    //   Projection operator
    //
    this->write_proj(p_FiffCov.projs);
    //
    //   Bad channels
    //
    if (p_FiffCov.bads.size() > 0) {
        this->start_block(FIFFB_MNE_BAD_CHANNELS);
        this->write_name_list(FIFF_MNE_CH_NAME_LIST, p_FiffCov.bads);
        this->end_block(FIFFB_MNE_BAD_CHANNELS);
    }
    //
    //   Done!
    //
    this->end_block(FIFFB_MNE_COV);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_ctf_comp(const QList<FiffCtfComp>& comps)
{
    fiff_long_t pos = tell();

    if (comps.size() <= 0)
        return -1;
    //
    //  This is very simple in fact
    //
    this->start_block(FIFFB_MNE_CTF_COMP);
    for (qint32 k = 0; k < comps.size(); ++k) {
        FiffCtfComp comp(comps[k]);
        this->start_block(FIFFB_MNE_CTF_COMP_DATA);
        //
        //    Write the compensation kind
        //
        this->write_int(FIFF_MNE_CTF_COMP_KIND, &comp.ctfkind);
        qint32 save_calibrated = comp.save_calibrated;
        this->write_int(FIFF_MNE_CTF_COMP_CALIBRATED, &save_calibrated);
        //
        //  Compensators are calibrated in memory; unless they are to be saved that way, undo it
        //  (mne _write_ctf_comp)
        //
        if (!comps[k].save_calibrated) {
            // A compensator built in memory has no calibration factors to undo
            if (comp.rowcals.size() == comp.data->data.rows())
                comp.data->data = comp.rowcals.reshaped().cwiseInverse().asDiagonal() * comp.data->data;
            if (comp.colcals.size() == comp.data->data.cols())
                comp.data->data = comp.data->data * comp.colcals.reshaped().cwiseInverse().asDiagonal();
        }

        this->write_named_matrix(FIFF_MNE_CTF_COMP_DATA, *comp.data.data());
        this->end_block(FIFFB_MNE_CTF_COMP_DATA);
    }
    this->end_block(FIFFB_MNE_CTF_COMP);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_dig_point(const FiffDigPoint& dig)
{
    fiff_long_t pos = tell();

    //?typedef struct _fiffDigPointRec {
    //  fiff_int_t kind;               /*!< FIFF_POINT_CARDINAL,
    //                                  *   FIFF_POINT_HPI, or
    //                                  *   FIFF_POINT_EEG */
    //  fiff_int_t ident;              /*!< Number identifying this point */
    //  fiff_float_t r[3];             /*!< Point location */
    //} *fiffDigPoint,fiffDigPointRec; /*!< Digitization point description */
    fiff_int_t datasize = 5 * 4;

    write(static_cast<qint32>(FIFF_DIG_POINT));
    write(static_cast<qint32>(FIFFT_DIG_POINT_STRUCT));
    write(static_cast<qint32>(datasize));
    write(static_cast<qint32>(FIFFV_NEXT_SEQ));

    //
    //   Start writing fiffDigPointRec
    //
    write(static_cast<qint32>(dig.kind));
    write(static_cast<qint32>(dig.ident));
    for (qint32 i = 0; i < 3; ++i)
        write(dig.r[i]);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_dir_pointer(fiff_int_t dirpos, fiff_long_t pos, fiff_int_t next)
{
    /*
     * Write entires to specified position
     */
    if (pos >= 0) {
        seekTo(pos);
    } else { //SEEK_END
        seekToEnd();
    }
    pos = tell();

    fiff_int_t datasize = 1 * 4;

    write(static_cast<qint32>(FIFF_DIR_POINTER));
    write(static_cast<qint32>(FIFFT_INT));
    write(static_cast<qint32>(datasize));
    write(static_cast<qint32>(next));

    write(dirpos);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_dir_entries(const std::vector<FiffDirEntry>& dir, fiff_long_t pos)
{
    //    /** Directories are composed of these structures. *
    //     typedef struct _fiffDirEntryRec {
    //      fiff_int_t  kind;		/**< Tag number *
    //      fiff_int_t  type;		/**< Data type *
    //      fiff_int_t  size;		/**< How many bytes *
    //      fiff_int_t  pos;		/**< Location in file
    //                      * Note: the data is located at pos +
    //                      * FIFFC_DATA_OFFSET *
    //     } fiffDirEntryRec,*fiffDirEntry;/**< Directory is composed of these *

    /*
     * Write entires to specified position
     */
    if (pos >= 0) {
        seekTo(pos);
    } else { //SEEK_END
        seekToEnd();
    }

    pos = tell();

    const auto nent = static_cast<fiff_int_t>(dir.size());
    fiff_int_t datasize = nent * static_cast<fiff_int_t>(sizeof(FiffDirEntry));

    write(static_cast<qint32>(FIFF_DIR));
    write(static_cast<qint32>(FIFFT_DIR_ENTRY_STRUCT));
    write(static_cast<qint32>(datasize));
    write(static_cast<qint32>(FIFFV_NEXT_NONE));

    //
    //   Start writing FiffDirEntries
    //
    for (qint32 i = 0; i < nent; ++i) {
        write(static_cast<qint32>(dir[i].kind));
        write(static_cast<qint32>(dir[i].type));
        write(static_cast<qint32>(dir[i].size));
        write(static_cast<qint32>(dir[i].pos));
    }

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_double(fiff_int_t kind, const double* data, fiff_int_t nel)
{
    fiff_long_t pos = tell();

    qint32 datasize = nel * 8;

    write(static_cast<qint32>(kind));
    write(static_cast<qint32>(FIFFT_DOUBLE));
    write(datasize);
    write(static_cast<qint32>(FIFFV_NEXT_SEQ));

    for (qint32 i = 0; i < nel; ++i)
        write(data[i]);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_float(fiff_int_t kind, const float* data, fiff_int_t nel)
{
    fiff_long_t pos = tell();

    qint32 datasize = nel * 4;

    write(static_cast<qint32>(kind));
    write(static_cast<qint32>(FIFFT_FLOAT));
    write(datasize);
    write(static_cast<qint32>(FIFFV_NEXT_SEQ));

    for (qint32 i = 0; i < nel; ++i)
        write(data[i]);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_float_matrix(fiff_int_t kind, const MatrixXf& mat)
{
    fiff_long_t pos = tell();

    qint32 numel = mat.rows() * mat.cols();

    fiff_int_t datasize = 4 * numel + 4 * 3;

    write(static_cast<qint32>(kind));
    write(static_cast<qint32> FIFFT_MATRIX_FLOAT);
    write(static_cast<qint32>(datasize));
    write(static_cast<qint32>(FIFFV_NEXT_SEQ));

    qint32 i, j;
    // Storage order: row-major
    for (i = 0; i < mat.rows(); ++i)
        for (j = 0; j < mat.cols(); ++j)
            write(mat(i, j));

    qint32 dims[3];
    dims[0] = mat.cols();
    dims[1] = mat.rows();
    dims[2] = 2;

    for (i = 0; i < 3; ++i)
        write(dims[i]);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_float_sparse_ccs(fiff_int_t kind, const SparseMatrix<float>& mat)
{
    fiff_long_t pos = tell();

    //
    //   nnz values
    //   nnz row indices
    //   ncol+1 pointers
    //   dims
    //   nnz
    //   ndim
    //
    qint32 nnzm = mat.nonZeros();
    qint32 ncol = mat.cols();
    fiff_int_t datasize = 4 * nnzm + 4 * nnzm + 4 * (ncol + 1) + 4 * 4;
    //
    //   Nonzero entries
    //
    using T = Eigen::Triplet<float>;
    std::vector<T> s;
    s.reserve(mat.nonZeros());
    for (int k = 0; k < mat.outerSize(); ++k)
        for (SparseMatrix<float>::InnerIterator it(mat, k); it; ++it)
            s.push_back(T(it.row(), it.col(), it.value()));

    s = Linalg::sortrows<float>(s, 1);

    //[ rows, starts ] = unique(s(:,1),'first');
    std::vector<qint32> cols, starts;
    qint32 v_old = -1;
    quint32 i;
    for (i = 0; i < s.size(); ++i) {
        if (static_cast<signed>(s[i].col()) != v_old) {
            v_old = s[i].col();
            cols.push_back(s[i].col());
            starts.push_back(i);
        }
    }

    write(static_cast<qint32>(kind));
    write(static_cast<qint32> FIFFT_CCS_MATRIX_FLOAT);
    write(static_cast<qint32>(datasize));
    write(static_cast<qint32>(FIFFV_NEXT_SEQ));

    //
    //  The data values
    //
    for (i = 0; i < s.size(); ++i)
        write(s[i].value());

    //
    //  Row indices
    //
    for (i = 0; i < s.size(); ++i)
        write(s[i].row());

    //
    //  Pointers
    //
    RowVectorXi ptrs = RowVectorXi::Ones(ncol + 1);
    ptrs.array() *= -1;
    quint32 k;
    for (k = 0; k < cols.size(); ++k)
        ptrs[cols[k]] = starts[k];
    ptrs[ncol] = nnzm;
    //
    //  Fill in pointers for empty columns
    //
    for (k = ncol; k >= 1; --k)
        if (ptrs[k - 1] < 0)
            ptrs[k - 1] = ptrs[k];
    //
    for (i = 0; i < static_cast<quint32>(ptrs.size()); ++i)
        write(ptrs[i]);
    //
    //   Dimensions
    //
    qint32 dims[4];
    dims[0] = mat.nonZeros();
    dims[1] = mat.rows();
    dims[2] = mat.cols();
    dims[3] = 2;

    for (i = 0; i < 4; ++i)
        write(dims[i]);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_float_sparse_rcs(fiff_int_t kind, const SparseMatrix<float>& mat)
{
    fiff_long_t pos = tell();

    //
    //   nnz values
    //   nnz column indices
    //   nrow+1 pointers
    //   dims
    //   nnz
    //   ndim
    //
    qint32 nnzm = mat.nonZeros();
    qint32 nrow = mat.rows();
    fiff_int_t datasize = 4 * nnzm + 4 * nnzm + 4 * (nrow + 1) + 4 * 4;
    //
    //   Nonzero entries
    //
    using T = Eigen::Triplet<float>;
    std::vector<T> s;
    s.reserve(mat.nonZeros());
    for (int k = 0; k < mat.outerSize(); ++k)
        for (SparseMatrix<float>::InnerIterator it(mat, k); it; ++it)
            s.push_back(T(it.row(), it.col(), it.value()));

    s = Linalg::sortrows<float>(s);

    //[ rows, starts ] = unique(s(:,1),'first');
    std::vector<qint32> rows, starts;
    qint32 v_old = -1;
    quint32 i;
    for (i = 0; i < s.size(); ++i) {
        if (static_cast<signed>(s[i].row()) != v_old) {
            v_old = s[i].row();
            rows.push_back(s[i].row());
            starts.push_back(i);
        }
    }

    //
    // Write tag info header
    //
    write(static_cast<qint32>(kind));
    write(static_cast<qint32> FIFFT_RCS_MATRIX_FLOAT);
    write(static_cast<qint32>(datasize));
    write(static_cast<qint32>(FIFFV_NEXT_SEQ));

    //
    //  The data values
    //
    for (i = 0; i < s.size(); ++i)
        write(s[i].value());

    //
    //  Column indices
    //
    for (i = 0; i < s.size(); ++i)
        write(s[i].col());

    //
    //  Pointers
    //
    RowVectorXi ptrs = RowVectorXi::Ones(nrow + 1);
    ptrs.array() *= -1;
    quint32 k;
    for (k = 0; k < rows.size(); ++k)
        ptrs[rows[k]] = starts[k];
    ptrs[nrow] = nnzm;

    //
    //  Fill in pointers for empty rows
    //
    for (k = nrow; k >= 1; --k)
        if (ptrs[k - 1] < 0)
            ptrs[k - 1] = ptrs[k];

    //
    for (i = 0; i < static_cast<quint32>(ptrs.size()); ++i)
        write(ptrs[i]);

    //
    //  Dimensions
    //
    qint32 dims[4];
    dims[0] = mat.nonZeros();
    dims[1] = mat.rows();
    dims[2] = mat.cols();
    dims[3] = 2;

    for (i = 0; i < 4; ++i)
        write(dims[i]);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_id(fiff_int_t kind, const FiffId& id)
{
    fiff_long_t pos = tell();

    FiffId t_id = id;

    if (t_id.isEmpty()) {
        //
        // Create a new one
        //
        t_id = FiffId::new_file_id();
    }

    //
    //
    fiff_int_t datasize = 5 * 4; //   The id comprises five integers

    write(static_cast<qint32>(kind));
    write(static_cast<qint32>(FIFFT_ID_STRUCT));
    write(static_cast<qint32>(datasize));
    write(static_cast<qint32>(FIFFV_NEXT_SEQ));
    //
    // Collect the bits together for one write
    //
    qint32 data[5];
    data[0] = t_id.version;
    data[1] = t_id.machid[0];
    data[2] = t_id.machid[1];
    data[3] = t_id.time.secs;
    data[4] = t_id.time.usecs;

    for (qint32 i = 0; i < 5; ++i)
        write(data[i]);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_info_base(const FiffInfoBase& p_FiffInfoBase)
{
    fiff_long_t pos = tell();

    //
    // Information from the MEG file
    //
    this->start_block(FIFFB_MNE_PARENT_MEAS_FILE);
    this->write_string(FIFF_MNE_FILE_NAME, p_FiffInfoBase.filename);
    if (!p_FiffInfoBase.meas_id.isEmpty())
        this->write_id(FIFF_PARENT_BLOCK_ID, p_FiffInfoBase.meas_id);

    //
    //    General
    //
    this->write_int(FIFF_NCHAN, &p_FiffInfoBase.nchan);

    //
    //    Channel info
    //
    qint32 k;
    QList<FiffChInfo> chs;
    for (k = 0; k < p_FiffInfoBase.nchan; ++k)
        chs << p_FiffInfoBase.chs[k];

    for (k = 0; k < p_FiffInfoBase.nchan; ++k) {
        //
        //    Scan numbers may have been messed up
        //
        chs[k].scanNo = k + 1; //+1 because
        this->write_ch_info(chs[k]);
    }

    //
    //    Blocks from the original -> skip this
    //
    bool have_hpi_result = false;

    //
    //    Coordinate transformations if the HPI result block was not there
    //
    if (!have_hpi_result) {
        if (!p_FiffInfoBase.dev_head_t.is_empty())
            this->write_coord_trans(p_FiffInfoBase.dev_head_t);
        if (!p_FiffInfoBase.ctf_head_t.is_empty())
            this->write_coord_trans(p_FiffInfoBase.ctf_head_t);
    }

    //
    //    Bad channels
    //
    if (p_FiffInfoBase.bads.size() > 0) {
        this->start_block(FIFFB_MNE_BAD_CHANNELS);
        this->write_name_list(FIFF_MNE_CH_NAME_LIST, p_FiffInfoBase.bads);
        this->end_block(FIFFB_MNE_BAD_CHANNELS);
    }

    this->end_block(FIFFB_MNE_PARENT_MEAS_FILE);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_int(fiff_int_t kind, const fiff_int_t* data, fiff_int_t nel, fiff_int_t next)
{
    fiff_long_t pos = tell();

    fiff_int_t datasize = nel * 4;

    write(static_cast<qint32>(kind));
    write(static_cast<qint32>(FIFFT_INT));
    write(static_cast<qint32>(datasize));
    write(static_cast<qint32>(next));

    for (qint32 i = 0; i < nel; ++i)
        write(data[i]);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_int_matrix(fiff_int_t kind, const MatrixXi& mat)
{
    fiff_long_t pos = tell();

    //    qint32 FIFFT_MATRIX = 1 << 30;
    //    qint32 FIFFT_MATRIX_INT = FIFFT_INT | FIFFT_MATRIX;

    qint32 numel = mat.rows() * mat.cols();

    fiff_int_t datasize = 4 * numel + 4 * 3;

    write(static_cast<qint32>(kind));
    write(static_cast<qint32> FIFFT_MATRIX_INT);
    write(static_cast<qint32>(datasize));
    write(static_cast<qint32>(FIFFV_NEXT_SEQ));

    qint32 i, j;
    // Storage order: row-major
    for (i = 0; i < mat.rows(); ++i)
        for (j = 0; j < mat.cols(); ++j)
            write(mat(i, j));

    qint32 dims[3];
    dims[0] = mat.cols();
    dims[1] = mat.rows();
    dims[2] = 2;

    for (i = 0; i < 3; ++i)
        write(dims[i]);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_name_list(fiff_int_t kind, const StringList& data)
{
    return this->write_string(kind, UTILSLIB::join(data, ":"));
}

//=============================================================================================================

fiff_long_t FiffStream::write_named_matrix(fiff_int_t kind, const FiffNamedMatrix& mat)
{
    fiff_long_t pos = tell();

    this->start_block(FIFFB_MNE_NAMED_MATRIX);
    this->write_int(FIFF_MNE_NROW, &mat.nrow);
    this->write_int(FIFF_MNE_NCOL, &mat.ncol);
    if (mat.row_names.size() > 0)
        this->write_name_list(FIFF_MNE_ROW_NAMES, mat.row_names);
    if (mat.col_names.size() > 0)
        this->write_name_list(FIFF_MNE_COL_NAMES, mat.col_names);
    this->write_float_matrix(kind, mat.data.cast<float>());
    this->end_block(FIFFB_MNE_NAMED_MATRIX);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_proj(const QList<FiffProj>& projs)
{
    fiff_long_t pos = tell();

    if (projs.size() <= 0)
        return -1;

    this->start_block(FIFFB_PROJ);

    for (auto& proj : projs) {
        this->start_block(FIFFB_PROJ_ITEM);
        this->write_string(FIFF_NAME, proj.desc.toStdString());
        this->write_int(FIFF_PROJ_ITEM_KIND, &proj.kind);
        if (proj.kind == FIFFV_PROJ_ITEM_FIELD) {
            float fValue = 0.0f;
            this->write_float(FIFF_PROJ_ITEM_TIME, &fValue);
        }

        this->write_int(FIFF_NCHAN, &proj.data->ncol);
        this->write_int(FIFF_PROJ_ITEM_NVEC, &proj.data->nrow);
        qint32 bValue = static_cast<qint32>(proj.active);
        this->write_int(FIFF_MNE_PROJ_ITEM_ACTIVE, &bValue);
        this->write_name_list(FIFF_PROJ_ITEM_CH_NAME_LIST, proj.data->col_names);
        this->write_float_matrix(FIFF_PROJ_ITEM_VECTORS, proj.data->data.cast<float>()); //rows == length(names)
        this->end_block(FIFFB_PROJ_ITEM);
    }
    this->end_block(FIFFB_PROJ);

    return pos;
}

//=============================================================================================================

fiff_long_t FiffStream::write_evoked_set(const FiffEvokedSet& p_FiffEvokedSet)
{
    fiff_long_t pos = tell();

    if (p_FiffEvokedSet.evoked.isEmpty())
        return -1;

    this->start_block(FIFFB_MEAS);
    this->write_id(FIFF_BLOCK_ID);

    if (p_FiffEvokedSet.info.meas_id.version != -1)
        this->write_id(FIFF_PARENT_BLOCK_ID, p_FiffEvokedSet.info.meas_id);

    //
    //  Write measurement info so that the file can be read back
    //
    this->start_block(FIFFB_MEAS_INFO);

    this->write_int(FIFF_NCHAN, &p_FiffEvokedSet.info.nchan);

    float sfreq = p_FiffEvokedSet.info.sfreq;
    this->write_float(FIFF_SFREQ, &sfreq);

    for (int k = 0; k < p_FiffEvokedSet.info.nchan; ++k) {
        FiffChInfo ch = p_FiffEvokedSet.info.chs[k];
        ch.scanNo = k + 1;
        this->write_ch_info(ch);
    }

    if (!p_FiffEvokedSet.info.dev_head_t.is_empty())
        this->write_coord_trans(p_FiffEvokedSet.info.dev_head_t);

    if (!p_FiffEvokedSet.info.projs.isEmpty())
        this->write_proj(p_FiffEvokedSet.info.projs);

    if (!p_FiffEvokedSet.info.bads.empty())
        this->write_bad_channels(p_FiffEvokedSet.info.bads);

    this->end_block(FIFFB_MEAS_INFO);

    for (int j = 0; j < p_FiffEvokedSet.evoked.size(); ++j) {
        const FiffEvoked& evoked = p_FiffEvokedSet.evoked[j];

        this->start_block(FIFFB_PROCESSED_DATA);
        this->start_block(FIFFB_EVOKED);

        this->write_string(FIFF_COMMENT, evoked.comment.toStdString());

        int first = evoked.first;
        this->write_int(FIFF_FIRST_SAMPLE, &first);

        int last = evoked.last;
        this->write_int(FIFF_LAST_SAMPLE, &last);

        this->start_block(FIFFB_ASPECT);

        int aspectKind = FIFFV_ASPECT_AVERAGE;
        this->write_int(FIFF_ASPECT_KIND, &aspectKind);

        int nave = evoked.nave;
        this->write_int(FIFF_NAVE, &nave);

        // Readers multiply FIFF_EPOCH by each channel's cal, so store uncalibrated values.
        Eigen::VectorXd decal(evoked.data.rows());
        for (int k = 0; k < decal.size(); ++k) {
            const double cal = k < p_FiffEvokedSet.info.chs.size() ? p_FiffEvokedSet.info.chs[k].cal : 1.0;
            decal[k] = cal != 0.0 ? 1.0 / cal : 1.0;
        }
        Eigen::MatrixXf floatData = (decal.asDiagonal() * evoked.data).cast<float>();
        this->write_float_matrix(FIFF_EPOCH, floatData);

        this->end_block(FIFFB_ASPECT);
        this->end_block(FIFFB_EVOKED);
        this->end_block(FIFFB_PROCESSED_DATA);
    }

    this->end_block(FIFFB_MEAS);

    return pos;
}

//=============================================================================================================

bool FiffStream::write_raw_buffer(const MatrixXd& buf,
                                  const RowVectorXd& cals)
{
    if (buf.rows() != cals.cols()) {
        qWarning("buffer and calibration sizes do not match\n");
        return false;
    }

    using T = Eigen::Triplet<double>;
    std::vector<T> tripletList;
    tripletList.reserve(cals.cols());
    for (qint32 i = 0; i < cals.cols(); ++i)
        tripletList.push_back(T(i, i, 1.0 / cals[i]));

    SparseMatrix<double> inv_calsMat(cals.cols(), cals.cols());
    inv_calsMat.setFromTriplets(tripletList.begin(), tripletList.end());

    MatrixXf tmp = (inv_calsMat * buf).cast<float>();
    this->write_float(FIFF_DATA_BUFFER, tmp.data(), tmp.rows() * tmp.cols());
    return true;
}

//=============================================================================================================

bool FiffStream::write_raw_buffer(const MatrixXd& buf,
                                  const SparseMatrix<double>& mult)
{
    if (buf.rows() != mult.cols()) {
        qWarning("buffer and mult sizes do not match\n");
        return false;
    }

    SparseMatrix<double> inv_mult(mult.rows(), mult.cols());
    for (int k = 0; k < inv_mult.outerSize(); ++k)
        for (SparseMatrix<double>::InnerIterator it(mult, k); it; ++it)
            inv_mult.coeffRef(it.row(), it.col()) = 1 / it.value();

    MatrixXf tmp = (inv_mult * buf).cast<float>();
    this->write_float(FIFF_DATA_BUFFER, tmp.data(), tmp.rows() * tmp.cols());
    return true;
}

//=============================================================================================================

bool FiffStream::write_raw_buffer(const MatrixXd& buf)
{
    MatrixXf tmp = buf.cast<float>();
    this->write_float(FIFF_DATA_BUFFER, tmp.data(), tmp.rows() * tmp.cols());
    return true;
}

//=============================================================================================================

fiff_long_t FiffStream::write_string(fiff_int_t kind,
                                     std::string_view data)
{
    fiff_long_t pos = tell();
    const fiff_int_t datasize = static_cast<fiff_int_t>(data.size());
    write(static_cast<qint32>(kind));
    write(static_cast<qint32>(FIFFT_STRING));
    write(static_cast<qint32>(datasize));
    write(static_cast<qint32>(FIFFV_NEXT_SEQ));

    writeRaw(data.data(), datasize);

    return pos;
}

//=============================================================================================================

void FiffStream::write_rt_command(fiff_int_t command, const QString& data)
{
    const QByteArray bytes = data.toUtf8();
    const fiff_int_t datasize = static_cast<fiff_int_t>(bytes.size());
    write(static_cast<qint32>(FIFF_MNE_RT_COMMAND));
    write(static_cast<qint32>(FIFFT_VOID));
    write(4 + static_cast<qint32>(datasize));
    write(static_cast<qint32>(FIFFV_NEXT_SEQ));
    write(command);

    writeRaw(bytes.constData(), datasize);
}

//=============================================================================================================

std::vector<FiffDirEntry> FiffStream::make_dir(bool* ok)
{
    std::vector<FiffDirEntry> dir;
    if (ok) {
        *ok = false;
    }
    if (!seekTo(SEEK_SET)) {
        return dir;
    }
    for (;;) {
        const fiff_long_t pos = tell();
        // A header read past the end of the data ends the scan.
        const auto tag = read_tag_info();
        if (!tag || tag->kind == FIFF_DIR) {
            break;
        }
        dir.push_back({tag->kind, tag->type, static_cast<fiff_int_t>(tag->size()), static_cast<fiff_int_t>(pos)});
        if (tag->next < 0) {
            break;
        }
    }
    dir.push_back(FiffDirEntry{});
    if (ok) {
        *ok = true;
    }
    return dir;
}

//=============================================================================================================

bool FiffStream::copyProcessingHistory(const QString& fromPath, const QString& toPath)
{
    // FiffStream::open() opens the device itself
    QFile srcFile(fromPath);
    FiffStream srcStream(&srcFile);
    if (!srcStream.open()) {
        qWarning("FiffStream::copyProcessingHistory - Cannot parse source FIFF file %s",
                 fromPath.toUtf8().constData());
        return false;
    }

    // Find the FIFFB_PROCESSING_HISTORY block in the directory tree
    std::vector<FiffDirNode::SPtr> histNodes = srcStream.dirtree()->dir_tree_find(FIFFB_PROCESSING_HISTORY);
    if (histNodes.empty()) {
        qWarning("FiffStream::copyProcessingHistory - No processing history block found in %s",
                 fromPath.toUtf8().constData());
        srcStream.close();
        return false;
    }

    // Read the whole block, including the processing records nested inside it
    struct Item
    {
        int blockStart = 0; // block kind to open, 0 for a tag
        int blockEnd = 0;   // block kind to close, 0 for a tag
        std::optional<FiffTag> tag;
    };
    std::vector<Item> items;
    std::function<void(const FiffDirNode::SPtr&)> collect = [&](const FiffDirNode::SPtr& node) {
        items.push_back({node->type, 0, std::nullopt});
        for (int i = 0; i < node->nent(); ++i) {
            const int kind = node->dir[i].kind;
            if (kind == FIFF_BLOCK_START || kind == FIFF_BLOCK_END)
                continue;
            if (auto tag = srcStream.read_tag(node->dir[i].pos))
                items.push_back({0, 0, std::move(tag)});
        }
        for (int c = 0; c < node->nchild(); ++c)
            collect(node->children[c]);
        items.push_back({0, node->type, std::nullopt});
    };
    collect(histNodes[0]);
    srcStream.close();

    // Open destination file for update
    QFile dstFile(toPath);
    auto dstStream = FiffStream::open_update(dstFile);
    if (!dstStream) {
        qWarning("FiffStream::copyProcessingHistory - Cannot open destination file %s for update",
                 toPath.toUtf8().constData());
        return false;
    }

    // open_update marks the last tag as the end of the file; chain it to the appended block
    const fiff_long_t lastPos = dstStream->dir()[dstStream->nent() - 2].pos;
    std::optional<FiffTag> lastTag;
    if (lastTag = dstStream->read_tag(lastPos); !lastTag) {
        dstStream->close();
        return false;
    }
    lastTag->next = FIFFV_NEXT_SEQ;
    dstStream->write_tag(*lastTag, lastPos);
    dstStream->device()->seek(dstStream->device()->size());

    for (const Item& item : items) {
        if (item.blockStart)
            dstStream->start_block(item.blockStart);
        else if (item.blockEnd)
            dstStream->end_block(item.blockEnd);
        else
            dstStream->write_tag(*item.tag);
    }
    // open_update left the file without its terminating tag
    dstStream->end_file();

    dstStream->close();
    return true;
}

//=============================================================================================================

std::optional<FiffTag> FiffStream::check_beginning()
{
    auto tag = read_tag();
    if (!tag || tag->kind != FIFF_FILE_ID || tag->type != FIFFT_ID_STRUCT || tag->size() != 20) {
        qWarning("Fiff::open: file does not start with a file id tag\n");
        return std::nullopt;
    }
    // Not rewound: a TCP stream cannot seek back, and the tag already holds the id.
    return tag;
}
