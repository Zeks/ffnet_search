/*
Flipper is a replacement search engine for fanfiction.net search results
Copyright (C) 2017-2019  Marchenko Nikolai

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>
*/
#pragma once

#include "include/queryinterfaces.h"

#include <QSqlDatabase>
#include <QReadWriteLock>
#include <queue>


namespace core{

struct IRNGGenerator{
    virtual ~IRNGGenerator(){}
    virtual QStringList Get(QSharedPointer<Query>, QString userToken, QSqlDatabase db, StoryFilter& filter)  = 0;
};

struct RNGList{
    QDateTime generationTimestamp;
    QDateTime lastAccessTimestamp;
    QString sequenceIdentifier;
    QStringList ids;
};

struct RNGData{
    RNGData();
    void Log(QString prefix);
    typedef QSharedPointer<RNGList> ListPtr;
    QHash<QString, ListPtr> randomIdLists;
    std::priority_queue<ListPtr,std::vector<ListPtr>, std::function<bool(ListPtr, ListPtr)>> queue;
    QReadWriteLock lock;
};

struct DefaultRNGgenerator : public IRNGGenerator{
    virtual QStringList Get(QSharedPointer<Query> where,
                        QString userToken,
                        QSqlDatabase db, StoryFilter& filter);

    void RemoveOutdatedRngSequences();
    void RemoveOlderRngSequencesPastTheLimit(uint32_t limit);

    QSharedPointer<RNGData> rngData;
    QSharedPointer<database::IDBWrapper> portableDBInterface;

};
}
