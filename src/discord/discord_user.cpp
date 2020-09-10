#include "discord/discord_user.h"
#include "discord/db_vendor.h"
#include "sql/discord/discord_queries.h"

using namespace std::chrono;
namespace discord{
User::User(QString userID, QString ffnID, QString name)
{
    InitFicsPtr();
    this->userID = userID;
    this->ffnID = ffnID;
    this->userName = name;
}

User::User(const User &other)
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
    this->page = user.page;
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

int User::CurrentPage() const
{
    QReadLocker locker(&lock);
    return page;
}

void User::SetPage(int newPage)
{
    QWriteLocker locker(&lock);
    page = newPage;
}

void User::AdvancePage(int value)
{
    QWriteLocker locker(&lock);
    lastEasyQuery = std::chrono::system_clock::now();
    page += value;
    if(page < 0)
        page = 0;
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

void User::SetPerfectRngFics(QSet<int> perfectRngFics)
{
    QWriteLocker locker(&lock);
    this->perfectRngFics = perfectRngFics;
}

void User::SetGoodRngFics(QSet<int> goodRngFics)
{
    QWriteLocker locker(&lock);
    this->goodRngFics = goodRngFics;
}

void User::SetPerfectRngScoreCutoff(int perfectRngScoreCutoff)
{
    QWriteLocker locker(&lock);
    this->perfectRngScoreCutoff = perfectRngScoreCutoff;
}

void User::SetGoodRngScoreCutoff(int goodRngScoreCutoff)
{
    QWriteLocker locker(&lock);
    this->goodRngScoreCutoff = goodRngScoreCutoff;
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

//void User::ToggleFandomIgnores(QSet<int> set)
//{
//    QWriteLocker locker(&lock);
//    for(auto fandom: set)
//    {
//        if(!ignoredFandoms.contains(fandom))
//            ignoredFandoms.insert(fandom);
//        else
//            ignoredFandoms.remove(fandom);
//    }
//}

void User::SetFandomFilter(int id, bool displayCrossovers)
{
    QWriteLocker locker(&lock);
    filteredFandoms.fandoms.insert(id);
    filteredFandoms.tokens.push_back({id, displayCrossovers});
}

void User::SetFandomFilter(FandomFilter filter)
{
    QWriteLocker locker(&lock);
    filteredFandoms = filter;
}

FandomFilter User::GetCurrentFandomFilter() const
{
    QReadLocker locker(&lock);
    return filteredFandoms;
}

void User::SetPositionsToIdsForCurrentPage(QHash<int, int> newData)
{
    QWriteLocker locker(&lock);
    positionToId = newData;
}

FandomFilter User::GetCurrentIgnoredFandoms() const
{
    QReadLocker locker(&lock);
    return ignoredFandoms;
}

void User::SetIgnoredFandoms(FandomFilter ignores)
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

void User::SetIgnoredFics(QSet<int> fics)
{
    QWriteLocker locker(&lock);
    ignoredFics = fics;
}

QSet<int> User::GetPerfectRngFics()
{
    QReadLocker locker(&lock);
    return perfectRngFics;
}

QSet<int> User::GetGoodRngFics()
{
    QReadLocker locker(&lock);
    return goodRngFics;
}

int User::GetPerfectRngScoreCutoff() const
{
    QReadLocker locker(&lock);
    return perfectRngScoreCutoff;
}

int User::GetGoodRngScoreCutoff() const
{
    QReadLocker locker(&lock);
    return goodRngScoreCutoff;
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
    return fics->matchCounts.size() > 0;
}

void User::SetFicList(core::RecommendationListFicData fics)
{
    QWriteLocker locker(&lock);
    *this->fics = fics;
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
void Users::AddUser(QSharedPointer<User> user)
{
    QWriteLocker locker(&lock);
    users[user->UserID()] = user;
}

bool Users::HasUser(QString user)
{
    QReadLocker locker(&lock);

    if(users.contains(user))
        return true;
    return false;
}

QSharedPointer<User> Users::GetUser(QString user)
{
    if(!users.contains(user))
        return {};
    return users[user];
}

bool Users::LoadUser(QString name)
{
    auto dbToken = An<discord::DatabaseVendor>()->GetDatabase("users");
    auto user = database::discord_queries::GetUser(dbToken->db, name).data;
    if(!user)
        return false;

    user->SetFandomFilter(database::discord_queries::GetFilterList(dbToken->db, name).data);
    user->SetIgnoredFandoms(database::discord_queries::GetFandomIgnoreList(dbToken->db, name).data);
    user->SetIgnoredFics(database::discord_queries::GetFicIgnoreList(dbToken->db, name).data);

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
    if(userVec.size() > 100)
        userVec.erase(userVec.begin()+25, userVec.end());
    users.clear();
    for(auto user: userVec)
        users[user->UserID()] = user;
}
}
