/*
obs-remote-deck
Copyright (C) 2026 Remote Deck

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "ws-transport.hpp"

#include <QAbstractSocket>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSslError>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>

namespace {

constexpr auto kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr int kMaxFrameBytes = 1024 * 1024;
constexpr int kMaxBufferBytes = 2 * kMaxFrameBytes;
constexpr int kMaxHandshakeBytes = 16 * 1024;
constexpr int kHandshakeTimeoutMs = 15000;

QByteArray randomMask()
{
	QByteArray key(4, 0);
	auto *rng = QRandomGenerator::global();
	for (int i = 0; i < 4; i++)
		key[i] = static_cast<char>(rng->bounded(256));
	return key;
}

void maskPayload(QByteArray &payload, const QByteArray &mask)
{
	for (int i = 0; i < payload.size(); i++)
		payload[i] = static_cast<char>(static_cast<unsigned char>(payload[i]) ^
					       static_cast<unsigned char>(mask[i % 4]));
}

QByteArray acceptKey(const QByteArray &secKey)
{
	return QCryptographicHash::hash(secKey + kWsGuid, QCryptographicHash::Sha1).toBase64();
}

QByteArray encodeFrame(quint8 opcode, const QByteArray &payload)
{
	QByteArray frame;
	frame.append(static_cast<char>(0x80 | (opcode & 0x0f)));

	const int len = payload.size();
	if (len <= 125) {
		frame.append(static_cast<char>(0x80 | len));
	} else if (len <= 0xffff) {
		frame.append(static_cast<char>(0x80 | 126));
		frame.append(static_cast<char>((len >> 8) & 0xff));
		frame.append(static_cast<char>(len & 0xff));
	} else {
		frame.append(static_cast<char>(0x80 | 127));
		for (int shift = 56; shift >= 0; shift -= 8)
			frame.append(static_cast<char>((static_cast<quint64>(len) >> shift) & 0xff));
	}

	const QByteArray mask = randomMask();
	frame.append(mask);
	QByteArray body = payload;
	maskPayload(body, mask);
	frame.append(body);
	return frame;
}

struct ParsedFrame {
	bool fin = true;
	quint8 opcode = 0;
	QByteArray payload;
};

enum class FrameResult { NeedMore, Ready, Error };

FrameResult takeFrame(QByteArray &buffer, ParsedFrame &out)
{
	if (buffer.size() < 2)
		return FrameResult::NeedMore;

	const auto *data = reinterpret_cast<const unsigned char *>(buffer.constData());
	const bool fin = (data[0] & 0x80) != 0;
	const quint8 opcode = data[0] & 0x0f;
	const bool masked = (data[1] & 0x80) != 0;
	quint64 payloadLen = data[1] & 0x7f;
	int offset = 2;

	if (payloadLen == 126) {
		if (buffer.size() < offset + 2)
			return FrameResult::NeedMore;
		payloadLen = (static_cast<quint64>(data[2]) << 8) | data[3];
		offset += 2;
	} else if (payloadLen == 127) {
		if (buffer.size() < offset + 8)
			return FrameResult::NeedMore;
		payloadLen = 0;
		for (int i = 0; i < 8; i++)
			payloadLen = (payloadLen << 8) | data[offset + i];
		offset += 8;
	}

	if (payloadLen > static_cast<quint64>(kMaxFrameBytes))
		return FrameResult::Error;

	if (masked)
		offset += 4;
	if (static_cast<quint64>(buffer.size()) < static_cast<quint64>(offset) + payloadLen)
		return FrameResult::NeedMore;

	QByteArray payload = buffer.mid(offset, static_cast<int>(payloadLen));
	if (masked) {
		const QByteArray mask = buffer.mid(offset - 4, 4);
		maskPayload(payload, mask);
	}

	buffer.remove(0, offset + static_cast<int>(payloadLen));
	out = ParsedFrame{fin, opcode, payload};
	return FrameResult::Ready;
}

}

WsTransport::WsTransport(QObject *parent) : QObject(parent)
{
	connectTimer = new QTimer(this);
	connectTimer->setSingleShot(true);
	connect(connectTimer, &QTimer::timeout, this, [this]() { fail(QStringLiteral("connection timed out")); });
}

WsTransport::~WsTransport()
{
	abortSocket();
}

void WsTransport::connectTo(const QUrl &url, const QList<QPair<QByteArray, QByteArray>> &headers)
{
	abortSocket();
	target = url;
	extraHeaders = headers;
	handshakeBuf.clear();
	frameBuf.clear();
	fragment.clear();
	fragmentOpcode = 0;
	handshaking = true;

	const QString scheme = url.scheme().toLower();
	const bool tls = scheme == QLatin1String("wss");
	const quint16 port = static_cast<quint16>(url.port(tls ? 443 : 80));

	if (tls) {
		auto *ssl = new QSslSocket(this);
		socket = ssl;
		connect(ssl, &QSslSocket::encrypted, this, &WsTransport::sendHandshake);
		connect(ssl, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
			Q_UNUSED(errors);
			fail(QStringLiteral("TLS handshake failed"));
		});
		ssl->connectToHostEncrypted(url.host(), port);
	} else {
		auto *tcp = new QTcpSocket(this);
		socket = tcp;
		connect(tcp, &QTcpSocket::connected, this, &WsTransport::sendHandshake);
		tcp->connectToHost(url.host(), port);
	}

	connect(socket, &QAbstractSocket::readyRead, this, &WsTransport::onReadyRead);
	connect(socket, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
		if (socket)
			fail(socket->errorString());
	});
	connect(socket, &QAbstractSocket::disconnected, this, [this]() { emit disconnected(); });

	connectTimer->start(kHandshakeTimeoutMs);
}

void WsTransport::disconnectFromServer()
{
	if (socket && socket->state() == QAbstractSocket::ConnectedState && !handshaking)
		socket->write(encodeFrame(0x8, {}));
	abortSocket();
}

void WsTransport::sendText(const QString &text)
{
	if (!socket || handshaking)
		return;
	socket->write(encodeFrame(0x1, text.toUtf8()));
}

void WsTransport::abortSocket()
{
	if (connectTimer)
		connectTimer->stop();
	if (!socket)
		return;
	socket->disconnect();
	socket->abort();
	socket->deleteLater();
	socket = nullptr;
	handshaking = false;
}

void WsTransport::fail(const QString &message)
{
	emit errorOccurred(message);
	abortSocket();
	emit disconnected();
}

void WsTransport::sendHandshake()
{
	QByteArray key(16, 0);
	auto *rng = QRandomGenerator::global();
	for (int i = 0; i < key.size(); i++)
		key[i] = static_cast<char>(rng->bounded(256));
	secKey = key.toBase64();
	expectedAccept = acceptKey(secKey);

	QString path = target.path();
	if (path.isEmpty())
		path = QStringLiteral("/");
	if (target.hasQuery())
		path += QLatin1Char('?') + target.query();

	QByteArray host = target.host().toUtf8();
	if (target.port() > 0)
		host += ':' + QByteArray::number(target.port());

	QByteArray req;
	req += "GET " + path.toUtf8() + " HTTP/1.1\r\n";
	req += "Host: " + host + "\r\n";
	req += "Upgrade: websocket\r\n";
	req += "Connection: Upgrade\r\n";
	req += "Sec-WebSocket-Key: " + secKey + "\r\n";
	req += "Sec-WebSocket-Version: 13\r\n";
	for (const auto &header : extraHeaders)
		req += header.first + ": " + header.second + "\r\n";
	req += "\r\n";
	socket->write(req);
}

void WsTransport::onReadyRead()
{
	if (!socket)
		return;
	if (handshaking)
		readHandshake();
	if (!handshaking && socket)
		readFrames();
}

void WsTransport::readHandshake()
{
	handshakeBuf += socket->readAll();
	const int end = handshakeBuf.indexOf("\r\n\r\n");
	if (end < 0) {
		if (handshakeBuf.size() > kMaxHandshakeBytes)
			fail(QStringLiteral("WebSocket handshake response too large"));
		return;
	}

	const QByteArray header = handshakeBuf.left(end);
	frameBuf = handshakeBuf.mid(end + 4);
	handshakeBuf.clear();

	const QList<QByteArray> lines = header.split('\n');
	if (lines.isEmpty() || !lines.first().contains("101")) {
		fail(QStringLiteral("upstream rejected the WebSocket upgrade"));
		return;
	}

	QByteArray accept;
	for (QByteArray line : lines) {
		line = line.trimmed();
		const int colon = line.indexOf(':');
		if (colon < 0)
			continue;
		if (QString::fromLatin1(line.left(colon)).compare(QLatin1String("Sec-WebSocket-Accept"),
								  Qt::CaseInsensitive) == 0)
			accept = line.mid(colon + 1).trimmed();
	}

	if (accept != expectedAccept) {
		fail(QStringLiteral("invalid WebSocket accept key"));
		return;
	}

	handshaking = false;
	connectTimer->stop();
	emit connected();
}

void WsTransport::readFrames()
{
	frameBuf += socket->readAll();
	while (true) {
		ParsedFrame parsed;
		const FrameResult result = takeFrame(frameBuf, parsed);
		if (result == FrameResult::Error) {
			fail(QStringLiteral("WebSocket frame too large"));
			return;
		}
		if (result == FrameResult::NeedMore) {
			if (frameBuf.size() > kMaxBufferBytes) {
				fail(QStringLiteral("WebSocket frame too large"));
				return;
			}
			break;
		}
		handleFrame(parsed.fin, parsed.opcode, parsed.payload);
		if (!socket)
			return;
	}
}

void WsTransport::handleFrame(bool fin, quint8 opcode, const QByteArray &payload)
{
	if (opcode == 0x0) {
		if (fragmentOpcode == 0) {
			fail(QStringLiteral("unexpected WebSocket continuation frame"));
			return;
		}
		if (fragment.size() + payload.size() > kMaxFrameBytes) {
			fail(QStringLiteral("WebSocket message too large"));
			return;
		}
		fragment += payload;
		if (fin)
			finishFragment();
		return;
	}
	if (opcode == 0x1 || opcode == 0x2) {
		if (!fin) {
			fragmentOpcode = opcode;
			fragment = payload;
			return;
		}
		if (opcode == 0x1)
			emit textMessageReceived(QString::fromUtf8(payload));
		return;
	}
	if (opcode == 0x8) {
		disconnectFromServer();
		return;
	}
	if (opcode == 0x9) {
		socket->write(encodeFrame(0xA, payload));
		return;
	}
}

void WsTransport::finishFragment()
{
	if (fragmentOpcode == 0x1)
		emit textMessageReceived(QString::fromUtf8(fragment));
	fragment.clear();
	fragmentOpcode = 0;
}
