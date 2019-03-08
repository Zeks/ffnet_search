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
#include <algorithm>
#include <QSqlQuery>
#include <QVariant>
#include <QDateTime>


#include "Interfaces/fandoms.h"
#include "Interfaces/db_interface.h"
#include "include/pure_sql.h"
#include "include/core/section.h"
#include "GlobalHeaders/run_once.h"
#include "include/transaction.h"
#include "include/container_utils.h"


namespace interfaces {

void Fandoms::Clear()
{
    this->recentFandoms.clear();
    this->fandoms.clear();
    this->updateQueue.clear();
    this->nameIndex.clear();
    this->idIndex.clear();
    this->trackedFandoms.clear();
    this->fandomsList.clear();
}

void Fandoms::ClearIndex()
{
    this->nameIndex.clear();
    this->idIndex.clear();
    this->trackedFandoms.clear();
    this->fandomsList.clear();
}

bool Fandoms::EnsureFandom(QString name)
{
    name = core::Fandom::ConvertName(name.trimmed());
    if(name.contains("'"))
        name = core::Fandom::ConvertName(name.trimmed());
    if(nameIndex.contains(name) || LoadFandom(name))
        return true;
    return false;
}

bool Fandoms::EnsureFandom(int id)
{
    if(idIndex.contains(id) || LoadFandom(id))
        return true;
    return false;
}
QSet<QString> Fandoms::EnsureFandoms(QList<core::FicPtr> fics)
{
    QSet<QString> uniqueFandoms;
    for(auto fic: fics)
    {
        if(!fic)
            continue;
        for(auto fandom : fic->fandoms)
        {
            fandom = core::Fandom::ConvertName(fandom.trimmed());
            if(!uniqueFandoms.contains(fandom))
                uniqueFandoms.insert(fandom);
        }
    }
    for(auto fandom: uniqueFandoms)
    {
        if(!EnsureFandom(fandom))
            CreateFandom(fandom);
    }
    for(auto fic: fics)
    {
        if(!fic)
            continue;
        for(auto fandom : fic->fandoms)
        {
            fandom = core::Fandom::ConvertName(fandom.trimmed());
            if(nameIndex.contains(fandom))
                fic->fandomIds.push_back(nameIndex[fandom]->id);
        }
    }
    return uniqueFandoms;
}

bool Fandoms::UploadFandomsIntoDatabase(QVector<core::Fandom> fandoms)
{
    bool result = true;
    database::Transaction transaction(db);
    for(auto fandom: fandoms)
    {
        core::FandomPtr fandomPtr(&fandom, [](core::Fandom*){});
        result = result && CreateFandom(fandomPtr, true, true);
    }
    transaction.finalize();
    return result;
}

bool Fandoms::RecalculateFandomStats(QStringList fandoms)
{
    bool success = true;
Q_UNUSED(fandoms)

    //todo
//    for(auto fandom : fandoms)
//    {
//        fandom = core::Fandom::ConvertName(fandom.trimmed());
//        success = success && database::puresql::UpdateFandomStats(GetIDForName(fandom), db);
//    }
    return success;
}

bool Fandoms::CreateFandom(core::FandomPtr fandom,
                           bool writeUrls,
                           bool useSuppliedIds )
{
    if(!fandom)
        return false;

    // getting empty node if its not there which is not a problem since we are
    // supposed to fill it at the end
    auto current = nameIndex[fandom->GetName()];
    if(current && current->id != -1)
        return true;
    database::Transaction transaction(db);
    auto result = database::puresql::CreateFandomInDatabase(fandom, db, writeUrls, useSuppliedIds);

    if(!result.success)
        return false;

    //fandom->id = portableDBInterface->GetLastIdForTable("fandoms");
    if(!transaction.finalize())
        return false;
    AddToIndex(fandom);
    return true;
}

bool Fandoms::CreateFandom(QString fandom)
{
    core::FandomPtr fandomPtr (new core::Fandom());
    fandom = core::Fandom::ConvertName(fandom);
    fandomPtr->SetName(fandom);
    return CreateFandom(fandomPtr);
}

bool Fandoms::AddFandomLink(QString fandom, core::Url url)
{
    fandom = core::Fandom::ConvertName(fandom);
    auto id = GetIDForName(fandom);
    if(id == -1)
        return false;
    auto result = database::puresql::AddUrlToFandom(id, url, db);
    return result.success;
}

core::FandomPtr Fandoms::GetFandom(QString name)
{
    core::FandomPtr result;
    name = core::Fandom::ConvertName(name);
    if(!name.trimmed().isEmpty() && EnsureFandom(name))
        result = nameIndex[name];

    return result;
}

void Fandoms::SetLastUpdateDate(QString fandom, QDate date)
{
    auto id = GetIDForName(fandom);
    if(id== -1)
        return;
    auto result = database::puresql::SetLastUpdateDateForFandom(id, date, db);
}

QStringList Fandoms::GetRecentFandoms()
{
    CleanupPtrList(recentFandoms);

    QStringList result;
    result.reserve(recentFandoms.size());

    std::sort(std::begin(recentFandoms), std::end(recentFandoms),[](auto f1, auto f2)->bool{
        return f1->idInRecentFandoms < f2->idInRecentFandoms;
    });

    std::for_each(std::begin(recentFandoms), std::end(recentFandoms), [&result](core::FandomPtr f){
        result.push_back(f->GetName());
    });
    return result;
}

void Fandoms::FillFandomList(bool forced)
{
    if(forced || fandomsList.isEmpty())
    {
        fandomsList  = database::puresql::GetFandomListFromDB(db).data;
    }
}


QStringList Fandoms::GetFandomList(bool forced)
{
    FillFandomList(forced);
    return fandomsList;
}


bool Fandoms::AddToTopOfRecent(QString fandom)
{
    fandom = core::Fandom::ConvertName(fandom);
    if(!EnsureFandom(fandom))
        return false;

    bool foundIterating = false;
    for(auto bit: recentFandoms)
    {
        if(bit->GetName() != fandom)
            bit->idInRecentFandoms = bit->idInRecentFandoms+1;
        else
        {
            bit->idInRecentFandoms = 0;
            foundIterating = true;
        }
    }
    if(!foundIterating)
    {
        recentFandoms.push_back(nameIndex[fandom]);
        nameIndex[fandom]->idInRecentFandoms = 0;
    }
    return true;
}




void Fandoms::PushFandomToTopOfRecent(QString fandom)
{
    fandom = core::Fandom::ConvertName(fandom);
    if(!EnsureFandom(fandom))
        return;

    AddToTopOfRecent(fandom);

    // needs to be done on Sync ideally
    // it's not a critical error to fail here, really
    database::Transaction transaction(db);
    portableDBInterface->PushFandomToTopOfRecent(fandom);
    portableDBInterface->RebaseFandomsToZero();
    transaction.finalize();
}

void Fandoms::RemoveFandomFromRecentList(QString name)
{
    name = core::Fandom::ConvertName(name);
    auto result = database::puresql::RemoveFandomFromRecentList(name, db);
}

bool Fandoms::IsDataLoaded()
{
    return isLoaded;
}

QList<core::FandomPtr> Fandoms::FilterFandoms(std::function<bool (core::FandomPtr)> f)
{
    QList<core::FandomPtr > result;
    if(!LoadAllFandoms())
        return result;

    result.reserve(fandoms.size()/2);
    for(auto fandom: fandoms)
    {
        if(f(fandom))
            result.push_back(fandom);
    }
    return result;
}

// do I really need that?
//bool Fandoms::Sync(bool forcedSync)
//{
//    bool ok = true;
//    for(auto fandom: fandoms)
//    {
//        if(forcedSync || fandom->hasChanges)
//        {
//            ok = ok && database::puresql::WriteMaxUpdateDateForFandom(fandom, db);
//        }
//    }
//    return ok;
//}

bool Fandoms::Load()
{
    Clear();
    // type makes it clearer
    QStringList recentFandoms = portableDBInterface->FetchRecentFandoms();
    recentFandoms.removeAll("");
    bool hadErrors = false;

    // not loading every fandom in this function
    // there is no point in doing that until its actually necessary for
    // some service operation
    // need to access the recent ones for sure though
    for(auto bit: recentFandoms)
    {
        bool fandomPresent = false;
        if(EnsureFandom(bit))
            fandomPresent = true;
        else
            hadErrors = true;

        if(fandomPresent)
            this->recentFandoms.push_back(nameIndex[bit]);
    }
    auto result = database::puresql::GetFandomCountInDatabase(db);
    fandomCount = result.data;
    return hadErrors || !result.success;
}

void Fandoms::ReloadRecentFandoms()
{
    this->recentFandoms.clear();
    QStringList recentFandoms = portableDBInterface->FetchRecentFandoms();
    recentFandoms.removeAll("");
    for(auto bit: recentFandoms)
    {
        bool fandomPresent = false;
        if(EnsureFandom(bit))
            fandomPresent = true;

        if(fandomPresent)
            this->recentFandoms.push_back(nameIndex[bit]);
    }
}

bool Fandoms::LoadTrackedFandoms(bool forced)
{
    bool result = false;
    bool needsLoading = trackedFandoms.empty()  || forced;
    if(!needsLoading)
        return true;

    auto opResult = database::puresql::GetTrackedFandomList(db);
    auto trackedList = opResult.data;
    if(trackedList.empty() || !opResult.success)
        return false;

    trackedFandoms.clear();
    trackedFandoms.reserve(trackedList.size());
    for(auto bit : trackedList)
    {
        bool localResult = EnsureFandom(bit);
        if(localResult)
            trackedFandoms.push_back(nameIndex[bit]);
        result = result && localResult;
    }
    return result;
}

bool Fandoms::LoadAllFandoms(bool forced)
{
    bool needsLoading = forced || fandoms.isEmpty();
    if(!needsLoading)
        return true;

    fandoms = database::puresql::GetAllFandoms(db).data;
    for(auto fandom: fandoms)
        indexFandomsById[fandom->id] = fandom->GetName();
    if(fandoms.empty())
        return false;
    return true;
}

QList<core::FandomPtr> Fandoms::LoadAllFandomsAfter(int id)
{
    return database::puresql::GetAllFandomsAfter(id, db).data;
}

bool Fandoms::IsTracked(QString fandom)
{
    fandom = core::Fandom::ConvertName(fandom.trimmed());
    if(!EnsureFandom(fandom))
        return false;
    return nameIndex[fandom]->tracked;
}

bool Fandoms::IgnoreFandom(QString name, bool includeCrossovers)
{
    auto id = GetIDForName(name);
    auto result = IgnoreFandom(id, includeCrossovers);
    return result;
}

bool Fandoms::IgnoreFandom(int id, bool includeCrossovers)
{
    auto result = database::puresql::IgnoreFandom(id,includeCrossovers,  db);
    return result.data;
}

QStringList Fandoms::GetIgnoredFandoms() const
{
    auto result = database::puresql::GetIgnoredFandoms(db);
    return result.data;
}

QHash<int, bool> Fandoms::GetIgnoredFandomsIDs() const
{
    auto result = database::puresql::GetIgnoredFandomIDs(db);
    return result.data;
}

QList<core::FandomPtr> Fandoms::GetAllLoadedFandoms()
{
    return fandoms;
}

QHash<int, QString> Fandoms::GetFandomNamesForIDs(QList<int> fandoms)
{
    return database::puresql::GetFandomNamesForIDs(fandoms, db).data;
}

bool Fandoms::FetchFandomsForFics(QVector<core::Fic> *fics)
{
    static bool loadFandoms = true;
    LoadAllFandoms(loadFandoms);
    loadFandoms = false;
    for(auto& fic: *fics)
    {
        for(int i = 0; i < fic.fandomIds.size(); i++)
        {
            auto index = fic.fandomIds[i];
            if(index != -1)
                fic.fandoms.push_back(indexFandomsById[fic.fandomIds[i]]);
        }
        fic.fandom = fic.fandoms.join(" & ");
    }
    return true;
}

bool Fandoms::RemoveFandomFromIgnoredList(QString name)
{
    auto id = GetIDForName(name);
    auto result = RemoveFandomFromIgnoredList(id);
    return result;
}

bool Fandoms::RemoveFandomFromIgnoredList(int id)
{
    auto result = database::puresql::RemoveFandomFromIgnoredList(id, db);
    return result.data;
}

bool Fandoms::IgnoreFandomSlashFilter(QString name)
{
    auto id = GetIDForName(name);
    auto result = IgnoreFandomSlashFilter(id);
    return result;
}

bool Fandoms::IgnoreFandomSlashFilter(int id)
{
    auto result = database::puresql::IgnoreFandomSlashFilter(id, db);
    return result.data;
}

QStringList Fandoms::GetIgnoredFandomsSlashFilter() const
{
    auto result = database::puresql::GetIgnoredFandomsSlashFilter(db);
    return result.data;
}

bool Fandoms::RemoveFandomFromIgnoredListSlashFilter(QString name)
{
    auto id = GetIDForName(name);
    auto result = RemoveFandomFromIgnoredListSlashFilter(id);
    return result;
}

bool Fandoms::RemoveFandomFromIgnoredListSlashFilter(int id)
{
    auto result = database::puresql::RemoveFandomFromIgnoredListSlashFilter(id, db);
    return result.data;
}

void Fandoms::Reindex()
{
    ClearIndex();
    nameIndex.reserve(fandoms.size());
    trackedFandoms.reserve(fandoms.size()/10);
    fandomsList.reserve(fandoms.size());
    idIndex.reserve(fandoms.size());
    for(auto fandom : fandoms)
        AddToIndex(fandom);
}

void Fandoms::AddToIndex(core::FandomPtr fandom)
{
    if(!fandom)
        return;
    nameIndex[fandom->GetName()] = fandom;
    idIndex[fandom->id] = fandom;
    fandomsList.push_back(fandom->GetName());
    if(fandom->tracked)
        trackedFandoms.push_back(fandom);
}

bool Fandoms::WipeFandom(QString name)
{
    name = core::Fandom::ConvertName(name.trimmed());
    if(!EnsureFandom(name))
        return false;
    return database::puresql::CleanupFandom(nameIndex[name]->id, db).success;
}

int Fandoms::GetFandomCount()
{
    return fandomCount;
}

int Fandoms::GetLastFandomID()
{
    return database::puresql::GetLastFandomID(db).data;
}

Fandoms::~Fandoms()
{

}

int Fandoms::GetIDForName(QString fandom)
{
    fandom = core::Fandom::ConvertName(fandom.trimmed());
    int result = -1;
    if(EnsureFandom(fandom))
        result = nameIndex[fandom]->id;
    return result;
}

QString Fandoms::GetNameForID(int id)
{
    QString result;
    if(EnsureFandom(id))
        result = idIndex[id]->GetName();
    return result;
}

void Fandoms::SetTracked(QString fandom, bool value, bool immediate)
{
    fandom = core::Fandom::ConvertName(fandom.trimmed());
    if(!EnsureFandom(fandom))
        return;

    if(immediate)
    {
        auto id = nameIndex[fandom]->id;
        database::puresql::SetFandomTracked(id, value, db);
    }
    else
        nameIndex[fandom]->hasChanges = nameIndex[fandom]->tracked == false;

    nameIndex[fandom]->tracked = value;
    if(value == true)
        trackedFandoms.push_back(nameIndex[fandom]);
    else
    {
        int id = nameIndex[fandom]->id;
        auto it = std::find_if(std::begin(trackedFandoms), std::end(trackedFandoms), [id](core::FandomPtr ptr){
                if(ptr && ptr->id == id)
                return true;
                return false;
    });
        if(it != std::end(trackedFandoms))
          trackedFandoms.erase(it);
    }
}

QStringList Fandoms::ListOfTrackedNames()
{
    QStringList names;
    if(!LoadTrackedFandoms())
        return names;
    for(auto fandom: trackedFandoms)
        if(fandom)
            names.push_back(fandom->GetName());
    return names;
}

QList<core::FandomPtr > Fandoms::ListOfTrackedFandoms()
{
    LoadTrackedFandoms();
    return trackedFandoms;
}



bool Fandoms::LoadFandom(QString name)
{
    name = core::Fandom::ConvertName(name);
    auto fandom = database::puresql::GetFandom(name, !isClient, db).data;
    if(!fandom)
        return false;

    fandoms.push_back(fandom);
    AddToIndex(fandom);
    return true;
}

bool Fandoms::LoadFandom(int id)
{
    auto fandom = database::puresql::GetFandom(id, !isClient, db).data;
    if(!fandom)
        return false;

    fandoms.push_back(fandom);
    AddToIndex(fandom);
    return true;
}

bool Fandoms::AssignTagToFandom(QString fandom, QString tag, bool includeCrosses)
{
    fandom = core::Fandom::ConvertName(fandom);
    if(!EnsureFandom(fandom))
        return false;

    auto id = nameIndex[fandom]->id;
    database::puresql::AssignTagToFandom(tag, id, db, includeCrosses);
    return true;
}

void Fandoms::CalculateFandomsAverages()
{
    //! todo needs refactoring
    //database::puresql::CalculateFandomsAverages(db);
}

void Fandoms::CalculateFandomsFicCounts()
{
    database::puresql::CalculateFandomsFicCounts(db);
}

}
