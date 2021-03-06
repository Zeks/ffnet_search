/*Flipper is a recommendation and search engine for fanfiction.net
Copyright (C) 2017-2020  Marchenko Nikolai

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>*/
#include "discord/discord_user.h"
#include "discord/db_vendor.h"
#include "sql/discord/discord_queries.h"

using namespace std::chrono;
namespace discord{
User::User(QString userID, QString ffnID, QString name, QString uuid):lock(QReadWriteLock::Recursive)
{
    InitFicsPtr();
    this->userID = userID;
    this->ffnID = ffnID;
    this->userName = name;
    this->uuid = uuid;
}

User::User(const User &other):lock(QReadWriteLock::Recursive)
{
    this->InitFicsPtr();
    *this = other;
}

void User::InitFicsPtr()
{
    fics = QSharedPointer<core::RecommendationListFicData>{new core::RecommendationListFicData};
}

User& User::operator=(const User &user)
{
    // self-assignment guard
    if (this == &user)
        return *this;
    this->userID = user.userID;
    this->ffnID = user.ffnID;
    this->currentRecsPage = user.currentRecsPage;
    this->banned = user.banned;
    this->lastRecsQuery = user.lastRecsQuery;
    this->lastEasyQuery = user.lastEasyQuery;
    // return the existing object so we can chain this operator
    return *this;
}

int User::secsSinceLastsRecQuery()
{
    QReadLocker locker(&lock);
    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - lastRecsQuery).count());
}

int User::secsSinceLastsEasyQuery()
{
    QReadLocker locker(&lock);
    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - lastEasyQuery).count());
}

int User::secsSinceLastsQuery()
{
    QReadLocker locker(&lock);
    if(lastRecsQuery > lastEasyQuery)
        return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - lastRecsQuery).count());
    else
        return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - lastEasyQuery).count());

}


void User::initNewRecsQuery()
{
    QWriteLocker locker(&lock);
    lastRecsQuery = std::chrono::system_clock::now();
}

void User::initNewEasyQuery()
{
    QWriteLocker locker(&lock);
    lastEasyQuery = std::chrono::system_clock::now();
}

int User::CurrentRecommendationsPage() const
{
    QReadLocker locker(&lock);
    return currentRecsPage;
}

void User::SetPage(int newPage)
{
    QWriteLocker locker(&lock);
    currentRecsPage = newPage;
}

void User::AdvancePage(int value, bool countAsQuery)
{
    QWriteLocker locker(&lock);
    if(countAsQuery)
        lastEasyQuery = std::chrono::system_clock::now();
    currentRecsPage += value;
    if(currentRecsPage < 0)
        currentRecsPage = 0;
}

bool User::HasUnfinishedRecRequest() const
{
    QReadLocker locker(&lock);
    return hasUnfinishedRecRequest;
}

void User::SetPerformingRecRequest(bool value)
{
    QWriteLocker locker(&lock);
    hasUnfinishedRecRequest = value;
}

void User::SetCurrentListId(int listId)
{
    QWriteLocker locker(&lock);
    this->listId = listId;
}

void User::SetBanned(bool banned)
{
    QWriteLocker locker(&lock);
    this->banned = banned;
}

void User::SetFfnID(QString id)
{
    QWriteLocker locker(&lock);
    this->ffnID = id;
}


void User::SetUserID(QString id)
{
    QWriteLocker locker(&lock);
    this->userID = id;
}

void User::SetUserName(QString name)
{
    QWriteLocker locker(&lock);
    this->userName = name;
}

void User::SetUuid(QString value)
{
    uuid = QUuid::fromString(value);
}

void User::SetFandomFilter(int id, bool displayCrossovers)
{
    QWriteLocker locker(&lock);
    filteredFandoms.fandoms.insert(id);
    filteredFandoms.tokens.push_back({id, displayCrossovers});
}

void User::SetFandomFilter(const FandomFilter& filter)
{
    QWriteLocker locker(&lock);
    filteredFandoms = filter;
}

FandomFilter User::GetCurrentFandomFilter() const
{
    QReadLocker locker(&lock);
    return filteredFandoms;
}

void User::SetPositionsToIdsForCurrentPage(const QHash<int, int>& newData)
{
    QWriteLocker locker(&lock);
    positionToId = newData;
}

FandomFilter User::GetCurrentIgnoredFandoms() const
{
    QReadLocker locker(&lock);
    return ignoredFandoms;
}

void User::SetIgnoredFandoms(const FandomFilter& ignores)
{
    QWriteLocker locker(&lock);
    ignoredFandoms = ignores;
}

int User::GetFicIDFromPositionId(int positionId) const
{
    QReadLocker locker(&lock);
    if(positionId == -1 || !positionToId.contains(positionId))
        return -1;
    return positionToId[positionId];
}

QSet<int> User::GetIgnoredFics()  const
{
    QReadLocker locker(&lock);
    return ignoredFics;
}

void User::SetIgnoredFics(const QSet<int>& fics)
{
    QWriteLocker locker(&lock);
    ignoredFics = fics;
}

void User::ResetFandomFilter()
{
    QWriteLocker locker(&lock);
    filteredFandoms = FandomFilter();
}


void User::ResetFandomIgnores()
{
    QWriteLocker locker(&lock);
    ignoredFandoms = FandomFilter();
}

void User::ResetFicIgnores()
{
    QWriteLocker locker(&lock);
    ignoredFics.clear();
}

QString User::FfnID() const
{
    QReadLocker locker(&lock);
    return ffnID;
}

QString User::UserName() const
{
    QReadLocker locker(&lock);
    return userName;
}

QString User::UserID() const
{
    QReadLocker locker(&lock);
    return userID;
}

QString User::GetUuid() const
{
    QReadLocker locker(&lock);
    return uuid.toString();
}

bool User::ReadsSlash()
{
    QReadLocker locker(&lock);
    return readsSlash;
}

bool User::HasActiveSet()
{
    QReadLocker locker(&lock);
    return fics && fics->metascores.size() > 0;
}

void User::SetFicList(QSharedPointer<core::RecommendationListFicData> fics)
{
    QWriteLocker locker(&lock);
    this->fics = fics;
}

QSharedPointer<core::RecommendationListFicData> User::FicList()
{
    QReadLocker locker(&lock);
    return fics;
}

system_clock::time_point User::LastActive()
{
    return lastRecsQuery > lastEasyQuery ? lastRecsQuery : lastEasyQuery;
}

int User::GetForcedMinMatch() const
{
    QReadLocker locker(&lock);
    return forcedMinMatch;
}

void User::SetForcedMinMatch(int value)
{
    QWriteLocker locker(&lock);
    forcedMinMatch = value;
}

int User::GetForcedRatio() const
{
    QReadLocker locker(&lock);
    return forcedRatio;
}

void User::SetForcedRatio(int value)
{
    QWriteLocker locker(&lock);
    forcedRatio = value;
}

bool User::GetUseLikedAuthorsOnly() const
{
    QReadLocker locker(&lock);
    return useLikedAuthorsOnly;
}

void User::SetUseLikedAuthorsOnly(bool value)
{
    QWriteLocker locker(&lock);
    useLikedAuthorsOnly = value;
}

bool User::GetSortFreshFirst() const
{
    QReadLocker locker(&lock);
    return sortFreshFirst;
}

void User::SetSortFreshFirst(bool value)
{
    QWriteLocker locker(&lock);
    if(value)
        sortGemsFirst = false;
    sortFreshFirst = value;
}

bool User::GetStrictFreshSort() const
{
    QReadLocker locker(&lock);
    return strictFreshSort;
}

void User::SetStrictFreshSort(bool value)
{
    QWriteLocker locker(&lock);
    strictFreshSort = value;
}

bool User::GetShowCompleteOnly() const
{
    QReadLocker locker(&lock);
    return showCompleteOnly;
}

void User::SetShowCompleteOnly(bool value)
{
    QWriteLocker locker(&lock);
    showCompleteOnly = value;
}

bool User::GetHideDead() const
{
    QReadLocker locker(&lock);
    return hideDead;
}

void User::SetHideDead(bool value)
{
    QWriteLocker locker(&lock);
    hideDead = value;
}

QString User::GetLastUsedRoll() const
{
    QReadLocker locker(&lock);
    return lastUsedRoll;
}

void User::SetLastUsedRoll(const QString &value)
{
    QWriteLocker locker(&lock);
    lastUsedRoll = value;
}

LastPageCommandMemo User::GetLastRecsPageMessage() const
{
    QReadLocker locker(&lock);
    return lastRecsPageCommandMemo;
}

void User::SetLastRecsPageMessage(const LastPageCommandMemo &value)
{
    QWriteLocker locker(&lock);
    lastRecsPageCommandMemo = value;
}

ECommandType User::GetLastPageType() const
{
    QReadLocker locker(&lock);
    return lastPageType;
}

void User::SetLastPageType(const ECommandType &value)
{
    QWriteLocker locker(&lock);
    lastPageType = value;
}

int User::GetSimilarFicsId() const
{
    QReadLocker locker(&lock);
    return similarFicsId;
}

void User::SetSimilarFicsId(int value)
{
    QWriteLocker locker(&lock);
    similarFicsId = value;
}

bool User::GetRngBustScheduled() const
{
    QReadLocker locker(&lock);
    return rngBustScheduled;
}

void User::SetRngBustScheduled(bool value)
{
    QWriteLocker locker(&lock);
    rngBustScheduled = value;
}

WordcountFilter User::GetWordcountFilter() const
{
    QReadLocker locker(&lock);
    return wordcountFilter;
}

void User::SetWordcountFilter(WordcountFilter value)
{
    QWriteLocker locker(&lock);
    wordcountFilter = value;
}

int User::GetDeadFicDaysRange() const
{
    QReadLocker locker(&lock);
    return deadFicDaysRange;
}

void User::SetDeadFicDaysRange(int value)
{
    QWriteLocker locker(&lock);
    deadFicDaysRange = value;
}

int User::GetCurrentHelpPage() const
{
    QReadLocker locker(&lock);
    return currentHelpPage;
}

void User::SetCurrentHelpPage(int value)
{
    QWriteLocker locker(&lock);
    currentHelpPage = value;
}

bool User::GetIsValid() const
{
    QReadLocker locker(&lock);
    return isValid;
}

void User::SetIsValid(bool value)
{
    QWriteLocker locker(&lock);
    isValid = value;
}

int User::GetFavouritesSize() const
{
    QReadLocker locker(&lock);
    return favouritesSize;
}

void User::SetFavouritesSize(int value)
{
    QWriteLocker locker(&lock);
    favouritesSize = value;
}

QDate User::GetLastLargeListRegenerationDate() const
{
    QReadLocker locker(&lock);
    return lastLargeListRegenerationDate;
}

void User::SetLastLargeListRegenerationDate(const QDate &value)
{
    QWriteLocker locker(&lock);
    lastLargeListRegenerationDate = value;
}

int User::GetLargeListCounter() const
{
    QReadLocker locker(&lock);
    return largeListCounter;
}

void User::SetLargeListCounter(int value)
{
    QWriteLocker locker(&lock);
    largeListCounter = value;
}

LargeListToken User::GetLargeListToken() const
{
    QReadLocker locker(&lock);
    return largeListToken;
}

void User::SetLargeListToken(const LargeListToken &value)
{
    QWriteLocker locker(&lock);
    largeListToken = value;
}

bool User::GetTimeoutWarningShown() const
{
    QReadLocker locker(&lock);
    return timeoutWarningShown;
}

void User::SetTimeoutWarningShown(bool value)
{
    QWriteLocker locker(&lock);
    timeoutWarningShown = value;
}

bool User::GetShownBannedMessage() const
{
    QReadLocker locker(&lock);
    return shownBannedMessage;
}

void User::SetShownBannedMessage(bool value)
{
    QWriteLocker locker(&lock);
    shownBannedMessage = value;
}

bool User::GetBanned() const
{
    QReadLocker locker(&lock);
    return banned;
}

bool User::GetSortGemsFirst() const
{
    QReadLocker locker(&lock);
    return sortGemsFirst;
}

void User::SetSortGemsFirst(bool value)
{
    QWriteLocker locker(&lock);
    if(value)
        sortFreshFirst = false;
    sortGemsFirst = value;
}

QString User::GetImpersonatedId() const
{
    QReadLocker locker(&lock);
    return impersonatedId;
}

void User::SetImpersonatedId(const QString &value)
{
    QWriteLocker locker(&lock);
    impersonatedId = value;
}

void User::SetRecommendationsCutoff(int value)
{
    QWriteLocker locker(&lock);
    recommednationsCutoff = value;
}

int User::GetRecommendationsCutoff() const
{
    QReadLocker locker(&lock);
    return recommednationsCutoff;
}

QString User::GetPublishedFilter() const
{
    QReadLocker locker(&lock);
    return publishedFilter;
}

void User::SetPublishedFilter(const QString &value)
{
    QWriteLocker locker(&lock);
    publishedFilter = value;
}

QString User::GetFinishedFilter() const
{
    QReadLocker locker(&lock);
    return finishedFilter;
}

void User::SetFinishedFilter(const QString &value)
{
    QWriteLocker locker(&lock);
    finishedFilter = value;
}

core::StoryFilter User::GetLastUsedStoryFilter() const
{
    QReadLocker locker(&lock);
    return lastUsedStoryFilter;
}

void User::SetLastUsedStoryFilter(const core::StoryFilter &value)
{
    QWriteLocker locker(&lock);
    lastUsedStoryFilter = value;
}

SleepyDiscord::Snowflake<SleepyDiscord::Message> User::GetLastHelpMessageID() const
{
    QReadLocker locker(&lock);
    return lastHelpMessageID;
}

void User::SetLastHelpMessageID(const SleepyDiscord::Snowflake<SleepyDiscord::Message> &value)
{
    QWriteLocker locker(&lock);
    lastHelpMessageID = value;
}

QSharedPointer<core::RecommendationListFicData> User::GetTemporaryFicsData() const
{
    QReadLocker locker(&lock);
    return temporaryFicsData;
}

void User::SetTemporaryFicsData(const QSharedPointer<core::RecommendationListFicData> &value)
{
    QWriteLocker locker(&lock);
    temporaryFicsData = value;
}

SleepyDiscord::Snowflake<SleepyDiscord::Message> User::GetLastAnyTypeMessageID() const
{
    QReadLocker locker(&lock);
    return lastAnyTypeMessageID;
}

void User::SetLastAnyTypeMessageID(const SleepyDiscord::Snowflake<SleepyDiscord::Message> &value)
{
    QWriteLocker locker(&lock);
    lastAnyTypeMessageID = value;
}

LastPageCommandMemo User::GetLastPostedListCommandMemo() const
{
    return lastPostedListCommandMemo;
}

void User::SetLastPostedListCommandMemo(const LastPageCommandMemo &value)
{
    lastPostedListCommandMemo = value;
}

void User::ScheduleForceNewRecsPage()
{

    QWriteLocker locker(&lock);
    forceNewPageNextTime = true;
}

bool User::NeedsNewRecsPage()
{
    QWriteLocker locker(&lock);
    if(forceNewPageNextTime){
        forceNewPageNextTime = false;
        return true;
    }
    return false;
}

void Users::AddUser(QSharedPointer<User> user)
{
    QWriteLocker locker(&lock);
    users[user->UserID()] = user;
}

void Users::RemoveUserData(QSharedPointer<User> user)
{
    QWriteLocker locker(&lock);
    users.remove(user->UserID());
}

bool Users::HasUser(QString user)
{
    QReadLocker locker(&lock);

    if(users.contains(user))
        return true;
    return false;
}

QSharedPointer<User> Users::GetUser(QString user) const
{
    auto it = users.find(user);
    if(it == users.cend())
        return {};
    return *it;
}

bool Users::LoadUser(QString name)
{
    auto dbToken = An<discord::DatabaseVendor>()->GetDatabase(QStringLiteral("users"));
    auto user = database::discord_queries::GetUser(dbToken->db, name).data;
    if(!user)
        return false;

    user->SetFandomFilter(database::discord_queries::GetFilterList(dbToken->db, name).data);
    user->SetIgnoredFandoms(database::discord_queries::GetFandomIgnoreList(dbToken->db, name).data);
    user->SetIgnoredFics(database::discord_queries::GetFicIgnoreList(dbToken->db, name).data);
    user->SetPage(database::discord_queries::GetCurrentPage(dbToken->db, name).data);

    users[name] = user;
    return true;
}


void Users::ClearInactiveUsers()
{
    QWriteLocker locker(&lock);
    auto userVec = users.values();
    std::sort(userVec.begin(), userVec.end(), [](const auto& user1, const auto& user2){
        return user1->LastActive() > user2->LastActive();
    });
    static const int activeUserDataLimit = 100;
    static const int startOfUserDataEraseRange = 25;
    if(userVec.size() > activeUserDataLimit){
        userVec.erase(userVec.begin()+startOfUserDataEraseRange, userVec.end());
        users.clear();
        for(const auto& user: userVec)
            users[user->UserID()] = user;
    }
}
}
