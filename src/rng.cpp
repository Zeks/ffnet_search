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
#include "include/rng.h"
#include "include/Interfaces/db_interface.h"
#include <functional>
#include <random>

namespace core{
QStringList DefaultRNGgenerator::Get(QSharedPointer<Query> query, QString userToken, QSqlDatabase, StoryFilter &filter)
{
    QString where = userToken + query->str;
    QStringList result;
    bool containsWhere = false;
    for(auto bind: query->bindings)
        where += bind.key + bind.value.toString().left(30);
    where += "Minrecs: " + QString::number(filter.minRecommendations);
    where += "Rated: " + QString::number(filter.rating);
    {
        // locking to make sure it's not modified when we search
        QReadLocker locker(&rngData->lock);
        containsWhere = rngData->randomIdLists.contains(where);
        containsWhere  = rngData->randomIdLists[where].generationDate > QDateTime::currentDateTimeUtc().addDays(-1);
    }

    QLOG_INFO() << "RANDOM USING WHERE:" << where;
    if(!containsWhere)
    {
        QWriteLocker locker(&rngData->lock);
        QLOG_INFO() << "GENERATING RANDOM SEQUENCE";
        auto idList = portableDBInterface->GetIdListForQuery(query);
        if(idList.size() == 0)
            idList.push_back("-1");
        rngData->randomIdLists[where].ids = idList;
        rngData->randomIdLists[where].generationDate = QDateTime::currentDateTimeUtc();
    }
    else
        QLOG_INFO() << "USING CACHED RANDOM SEQUENCE";

    std::random_device rd; // obtain a random number from hardware
    std::mt19937 eng(rd()); // seed the generator
    auto& currentList = rngData->randomIdLists[where].ids;
    std::uniform_int_distribution<> distr(0, currentList.size()-1); // define the range
    for(auto i = 0; i < filter.maxFics; i++)
    {
        auto value = distr(eng);
        result.push_back(currentList[value]);
    }
    return result;
}
}
