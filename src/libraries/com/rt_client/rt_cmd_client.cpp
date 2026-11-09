//=============================================================================================================
/**
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 MNE-CPP Authors
 *
 * @file     rt_cmd_client.cpp
 * @author   Christoph Dinh <christoph.dinh@mne-cpp.org>
 * @since    2.0.0
 * @date     March 2026
 * @brief    Translation unit for @ref COMLIB::RtCmdClient: command-port (4217) I/O and reply accumulation.
 *
 * Owns the wire-level encoding of outbound requests (CLI string vs.
 * @c QJsonDocument), drains incoming reply bytes into
 * @c m_sAvailableData under @c m_qMutex, and synthesises the
 * convenience requests used by the @ref RtClient handshake —
 * @c requestBufsize(), @c requestCommands() and @c requestConnectors().
 * The embedded @ref CommandManager is populated from the server’s
 * advertised command list so subscript lookup matches what the server
 * actually accepts.
 */

//=============================================================================================================
// INCLUDES
//=============================================================================================================

#include "rt_cmd_client.h"
#include <base/log.h>

//=============================================================================================================
// QT INCLUDES
//=============================================================================================================

#include <QDeadlineTimer>
#include <QThread>

//=============================================================================================================
// USED NAMESPACES
//=============================================================================================================

using namespace COMLIB;

//=============================================================================================================
// DEFINE STATIC METHODS
//=============================================================================================================

namespace
{

constexpr int kReplyTimeoutMs = 30000; /**< Longest wait for the reply to a command, e.g. while the server loads a file. */

} // namespace

//=============================================================================================================
// DEFINE MEMBER METHODS
//=============================================================================================================

RtCmdClient::RtCmdClient(QObject* parent)
: QTcpSocket(parent)
{
    QObject::connect(&m_commandManager, &CommandManager::triggered, this,
                     &RtCmdClient::sendCommandJSON);
}

//=============================================================================================================

QString RtCmdClient::sendCLICommand(const QString& p_sCommand)
{
    QString t_sCommand = QString("%1\n").arg(p_sCommand);
    QString p_sReply;

    if (this->state() == QAbstractSocket::ConnectedState) {
        this->write(t_sCommand.toUtf8().constData(), t_sCommand.size());
        this->waitForBytesWritten();

        //thats not the most elegant way
        this->waitForReadyRead(1000);
        QByteArray t_qByteArrayRaw;
        // TODO(cpieloth): We need a break condition e.g. last byte == \0 or \n
        // Large responses can be split to more than one packet which could be a problem on big network latencies.
        while (this->bytesAvailable() > 0 && this->canReadLine())
            t_qByteArrayRaw += this->readAll();

        p_sReply = QString(t_qByteArrayRaw);
    }
    return p_sReply;
}

//=============================================================================================================

void RtCmdClient::sendCommandJSON(const Command& p_command)
{
    const QString t_sCommand = QString("{\"commands\":{%1}}\n").arg(p_command.toStringReadySend());

    QString t_sReply;

    if (this->state() == QAbstractSocket::ConnectedState) {
        // Request and reply are QDataStream strings behind a quint16 byte count
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_5_1);
        out << static_cast<quint16>(0);
        out << t_sCommand;
        out.device()->seek(0);
        out << static_cast<quint16>(static_cast<unsigned long>(block.size()) - sizeof(quint16));
        this->write(block);
        this->waitForBytesWritten();

        QDataStream in(this);
        in.setVersion(QDataStream::Qt_5_1);
        quint16 blockSize = 0;
        const QDeadlineTimer deadline(kReplyTimeoutMs);
        while (true) {
            if (blockSize == 0 && this->bytesAvailable() >= static_cast<qint64>(sizeof(quint16))) {
                in >> blockSize;
            }
            if (blockSize > 0 && this->bytesAvailable() >= blockSize) {
                in >> t_sReply;
                break;
            }
            // Stop when the server is gone or does not answer; waitForReadyRead also returns on disconnection
            if (this->state() != QAbstractSocket::ConnectedState || deadline.hasExpired() ||
                !this->waitForReadyRead(static_cast<int>(deadline.remainingTime()))) {
                BASELIB::Log::warning("No reply to {}: the server closed the connection or did not answer",
                                      p_command.command().toStdString());
                break;
            }
        }
    } else {
        BASELIB::Log::warning("Request was not send, because client is not connected!");
    }

    m_qMutex.lock();
    m_sAvailableData = t_sReply;
    m_qMutex.unlock();

    emit response(t_sReply);
}

//=============================================================================================================

qint32 RtCmdClient::requestBufsize()
{
    //Send
    m_commandManager["getbufsize"].send();

    //Receive
    m_qMutex.lock();
    QByteArray t_sJsonCommands = m_sAvailableData.toUtf8();
    m_qMutex.unlock();

    //Parse
    QJsonParseError error;
    QJsonDocument t_jsonDocumentOrigin = QJsonDocument::fromJson(t_sJsonCommands, &error);

    if (error.error == QJsonParseError::NoError) {
        //Switch to command object
        if (t_jsonDocumentOrigin.isObject() && t_jsonDocumentOrigin.object().value(QString("bufsize")) != QJsonValue::Undefined) {
            qint32 size = static_cast<qint32>(t_jsonDocumentOrigin.object().value(QString("bufsize")).toDouble());
            return size;
        }
    }

    BASELIB::Log::critical("Unable to parse JSON response: {}", error.errorString().toStdString());
    return -1;
}

//=============================================================================================================

void RtCmdClient::requestCommands()
{
    //No commands are present -> thats why help has to be send using a self created command
    const QString help("help");
    const QString description("");
    const Command cmdHelp(help, description);
    this->sendCommandJSON(cmdHelp);

    //Clear Commands
    m_commandManager.clear();

    //Receive
    m_qMutex.lock();
    QByteArray t_sJsonCommands = m_sAvailableData.toUtf8();
    m_qMutex.unlock();

    //Parse
    QJsonParseError error;
    QJsonDocument t_jsonDocumentOrigin = QJsonDocument::fromJson(
        t_sJsonCommands, &error);

    if (error.error == QJsonParseError::NoError)
        m_commandManager.insert(t_jsonDocumentOrigin);
    else
        BASELIB::Log::critical("Unable to parse JSON response: {}", error.errorString().toStdString());
}

//=============================================================================================================

qint32 RtCmdClient::requestConnectors(QMap<qint32, QString>& p_qMapConnectors)
{
    //Send
    m_commandManager["conlist"].send();

    //Receive
    m_qMutex.lock();
    QByteArray t_sJsonConnectors = m_sAvailableData.toUtf8();
    m_qMutex.unlock();

    //Parse
    QJsonParseError error;
    QJsonDocument t_jsonDocumentOrigin = QJsonDocument::fromJson(
        t_sJsonConnectors, &error);

    QJsonObject t_jsonObjectConnectors;

    //Switch to command object
    if (t_jsonDocumentOrigin.isObject() && t_jsonDocumentOrigin.object().value(QString("connectors")) != QJsonValue::Undefined)
        t_jsonObjectConnectors = t_jsonDocumentOrigin.object().value(QString("connectors")).toObject();

    //inits
    qint32 p_iActiveId = -1;
    p_qMapConnectors.clear();

    //insert connectors
    QJsonObject::Iterator it;
    for (it = t_jsonObjectConnectors.begin(); it != t_jsonObjectConnectors.end(); ++it) {
        QString t_qConnectorName = it.key();

        qint32 id = it.value().toObject().value(QString("id")).toDouble();

        if (!p_qMapConnectors.contains(id))
            p_qMapConnectors.insert(id, t_qConnectorName);
        else
            BASELIB::Log::warning("Warning: CommandMap contains command {} already. Insertion skipped.\n", it.key().toStdString());

        //if connector is active indicate it
        if (it.value().toObject().value(QString("active")).toBool())
            p_iActiveId = id;
    }

    return p_iActiveId;
}

////=============================================================================================================

//void RtCmdClient::requestMeasInfo(qint32 p_id)
//{
//    QString t_sCommand = QString("measinfo %1").arg(p_id);
//    this->sendCommand(t_sCommand);
//}

////=============================================================================================================

//void RtCmdClient::requestMeasInfo(const QString &p_Alias)
//{
//    QString t_sCommand = QString("measinfo %1").arg(p_Alias);
//    this->sendCommand(t_sCommand);
//}

////=============================================================================================================

//void RtCmdClient::requestMeas(qint32 p_id)
//{
//    QString t_sCommand = QString("start %1").arg(p_id);
//    this->sendCommand(t_sCommand);
//}

////=============================================================================================================

//void RtCmdClient::requestMeas(QString p_Alias)
//{
//    QString t_sCommand = QString("start %1").arg(p_Alias);
//    this->sendCommand(t_sCommand);
//}

////=============================================================================================================

//void RtCmdClient::stopAll()
//{
//    QString t_sCommand = QString("stop-all");
//    this->sendCommand(t_sCommand);
//}

//=============================================================================================================

bool RtCmdClient::waitForDataAvailable(qint32 msecs) const
{
    if (m_sAvailableData.size() > 0)
        return true;

    const QDeadlineTimer deadline(msecs); // -1 never expires
    while (!deadline.hasExpired()) {
        QThread::msleep(5);
        if (m_sAvailableData.size() > 0)
            return true;
    }
    return false;
}

//=============================================================================================================

Command& RtCmdClient::operator[](const QString& key)
{
    return m_commandManager[key];
}

//=============================================================================================================

const Command RtCmdClient::operator[](const QString& key) const
{
    return m_commandManager[key];
}
