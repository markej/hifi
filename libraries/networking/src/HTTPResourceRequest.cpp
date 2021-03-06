//
//  HTTPResourceRequest.cpp
//  libraries/networking/src
//
//  Created by Ryan Huffman on 2015/07/23
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "HTTPResourceRequest.h"

#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <SharedUtil.h>

#include "NetworkAccessManager.h"
#include "NetworkLogging.h"

HTTPResourceRequest::~HTTPResourceRequest() {
    if (_reply) {
        _reply->disconnect(this);
        _reply->deleteLater();
        _reply = nullptr;
    }
}

void HTTPResourceRequest::setupTimer() {
    Q_ASSERT(!_sendTimer);
    static const int TIMEOUT_MS = 10000;

    _sendTimer = new QTimer();
    connect(this, &QObject::destroyed, _sendTimer, &QTimer::deleteLater);
    connect(_sendTimer, &QTimer::timeout, this, &HTTPResourceRequest::onTimeout);

    _sendTimer->setSingleShot(true);
    _sendTimer->start(TIMEOUT_MS);
}

void HTTPResourceRequest::cleanupTimer() {
    Q_ASSERT(_sendTimer);
    _sendTimer->disconnect(this);
    _sendTimer->deleteLater();
    _sendTimer = nullptr;
}

void HTTPResourceRequest::doSend() {
    QNetworkRequest networkRequest(_url);
    networkRequest.setHeader(QNetworkRequest::UserAgentHeader, HIGH_FIDELITY_USER_AGENT);

    if (_cacheEnabled) {
        networkRequest.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    } else {
        networkRequest.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    }

    _reply = NetworkAccessManager::getInstance().get(networkRequest);
    
    connect(_reply, &QNetworkReply::finished, this, &HTTPResourceRequest::onRequestFinished);
    connect(_reply, &QNetworkReply::downloadProgress, this, &HTTPResourceRequest::onDownloadProgress);

    setupTimer();
}

void HTTPResourceRequest::onRequestFinished() {
    Q_ASSERT(_state == InProgress);
    Q_ASSERT(_reply);

    cleanupTimer();
    
    switch(_reply->error()) {
        case QNetworkReply::NoError:
            _data = _reply->readAll();
            _loadedFromCache = _reply->attribute(QNetworkRequest::SourceIsFromCacheAttribute).toBool();
            _result = Success;
            break;
        case QNetworkReply::TimeoutError:
            _result = Timeout;
            break;
        default:
            _result = Error;
            break;
    }
    _reply->disconnect(this);
    _reply->deleteLater();
    _reply = nullptr;
    
    _state = Finished;
    emit finished();
}

void HTTPResourceRequest::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    Q_ASSERT(_state == InProgress);
    
    // We've received data, so reset the timer
    _sendTimer->start();

    emit progress(bytesReceived, bytesTotal);
}

void HTTPResourceRequest::onTimeout() {
    Q_ASSERT(_state == InProgress);
    _reply->disconnect(this);
    _reply->abort();
    _reply->deleteLater();
    _reply = nullptr;

    cleanupTimer();
    
    _result = Timeout;
    _state = Finished;
    emit finished();
}
