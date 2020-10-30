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
#include <QString>
#include <QScopedPointer>
#include "GlobalHeaders/SingletonHolder.h"
#include "include/webpage.h"
#include "ECacheMode.h"

namespace discord {
class PageGetterPrivate;
class LockedDatabase;
class PageManager
{
    public:
    typedef std::function<QSharedPointer<LockedDatabase>()> DBGetterFunc;
    PageManager();
    ~PageManager();
    void SetDatabaseGetter(DBGetterFunc dbGetter);
    void SetCachedMode(bool value);
    bool GetCachedMode() const;
    WebPage GetPage(QString url, ECacheMode useCache = ECacheMode::dont_use_cache);
    void SavePageToDB(const WebPage & page);
    void SetAutomaticCacheLimit(QDate);


    QScopedPointer<PageGetterPrivate> d;
};
}
BIND_TO_SELF_SINGLE(discord::PageManager);