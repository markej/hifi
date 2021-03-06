//
//  AssetRequest.cpp
//  libraries/networking/src
//
//  Created by Ryan Huffman on 2015/07/24
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "AssetRequest.h"

#include <algorithm>

#include <QtCore/QThread>

#include "AssetClient.h"
#include "NetworkLogging.h"
#include "NodeList.h"
#include "ResourceCache.h"

AssetRequest::AssetRequest(const QString& hash, const QString& extension) :
    QObject(),
    _hash(hash),
    _extension(extension)
{
}

void AssetRequest::start() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "start", Qt::AutoConnection);
        return;
    }

    if (_state != NotStarted) {
        qCWarning(asset_client) << "AssetRequest already started.";
        return;
    }
    
    // Try to load from cache
    _data = loadFromCache(getUrl());
    if (!_data.isNull()) {
        _info.hash = _hash;
        _info.size = _data.size();
        _error = NoError;
        
        _state = Finished;
        emit finished(this);
        return;
    }
    
    _state = WaitingForInfo;
    
    auto assetClient = DependencyManager::get<AssetClient>();
    assetClient->getAssetInfo(_hash, _extension, [this](bool responseReceived, AssetServerError serverError, AssetInfo info) {
        _info = info;
        
        if (!responseReceived) {
            _error = NetworkError;
        } else if (serverError != AssetServerError::NoError) {
            switch(serverError) {
                case AssetServerError::AssetNotFound:
                    _error = NotFound;
                    break;
                default:
                    _error = UnknownError;
                    break;
            }
        }

        if (_error != NoError) {
            qCWarning(asset_client) << "Got error retrieving asset info for" << _hash;
            _state = Finished;
            emit finished(this);
            
            return;
        }
        
        _state = WaitingForData;
        _data.resize(info.size);
        
        qCDebug(asset_client) << "Got size of " << _hash << " : " << info.size << " bytes";
        
        int start = 0, end = _info.size;
        
        auto assetClient = DependencyManager::get<AssetClient>();
        assetClient->getAsset(_hash, _extension, start, end, [this, start, end](bool responseReceived, AssetServerError serverError,
                                                                                const QByteArray& data) {
            if (!responseReceived) {
                _error = NetworkError;
            } else if (serverError != AssetServerError::NoError) {
                switch (serverError) {
                    case AssetServerError::AssetNotFound:
                        _error = NotFound;
                        break;
                    case AssetServerError::InvalidByteRange:
                        _error = InvalidByteRange;
                        break;
                    default:
                        _error = UnknownError;
                        break;
                }
            } else {
                Q_ASSERT(data.size() == (end - start));
                
                // we need to check the hash of the received data to make sure it matches what we expect
                if (hashData(data).toHex() == _hash) {
                    memcpy(_data.data() + start, data.constData(), data.size());
                    _totalReceived += data.size();
                    emit progress(_totalReceived, _info.size);
                    
                    saveToCache(getUrl(), data);
                } else {
                    // hash doesn't match - we have an error
                    _error = HashVerificationFailed;
                }
                
            }
            
            if (_error != NoError) {
                qCWarning(asset_client) << "Got error retrieving asset" << _hash << "- error code" << _error;
            }
            
            _state = Finished;
            emit finished(this);
        }, [this](qint64 totalReceived, qint64 total) {
            emit progress(totalReceived, total);
        });
    });
}
