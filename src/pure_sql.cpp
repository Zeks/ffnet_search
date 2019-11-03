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
#include "pure_sql.h"
#include "transaction.h"
#include "core/section.h"
#include "pagetask.h"
#include "url_utils.h"
#include "Interfaces/genres.h"
#include "EGenres.h"
#include "include/in_tag_accessor.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVector>
#include <QVariant>
#include <QDebug>
#include <algorithm>
#include<QSqlDriver>
#include<QSqlDatabase>
namespace database {
namespace puresql{

template <typename T>
bool NullPtrGuard(T item)
{
    if(!item)
    {
        qDebug() << "attempted to fill a nullptr";
        return false;
    }
    return true;
}
#define DATAQ [&](auto& data, auto q)
#define DATAQN [&](auto& , auto q)
#define COMMAND(NAME)  { #NAME, NAME}

#define BP1(X) {COMMAND(X)}
#define BP2(X, Y) {COMMAND(X), COMMAND(Y)}
#define BP3(X, Y, Z) {COMMAND(X), COMMAND(Y), COMMAND(Z)}
#define BP4(X, Y, Z, W) {COMMAND(X), COMMAND(Y), COMMAND(Z), COMMAND(W)}

//#define BP4(X, Y, Z, W) {{"X", X}, {"Y", Y},{"Z", Z},{"W", W}}

static DiagnosticSQLResult<FicIdHash> GetGlobalIDHash(QSqlDatabase db, QString where)
{
    QString qs = QString("select id, ffn_id, ao3_id, sb_id, sv_id from fanfics ");
    if(!where.isEmpty())
        qs+=where;

    SqlContext<FicIdHash> ctx(db, qs);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data.ids["ffn"][q.value("ffn_id").toInt()] = q.value("id").toInt();
        ctx.result.data.ids["sb"][q.value("sb_id").toInt()] = q.value("id").toInt();
        ctx.result.data.ids["sv"][q.value("sv_id").toInt()] = q.value("id").toInt();
        ctx.result.data.ids["ao3"][q.value("ao3_id").toInt()] = q.value("id").toInt();
        FanficIdRecord rec;
        rec.ids["ffn"] = q.value("ffn_id").toInt();
        rec.ids["sb"] = q.value("sb_id").toInt();
        rec.ids["sv"] = q.value("sv_id").toInt();
        rec.ids["ao3"] = q.value("ao3_id").toInt();
        ctx.result.data.records[q.value("id").toInt()] = rec;
    });
    return ctx.result;

}
static const FanficIdRecord& GrabFicIDFromQuery(QSqlQuery& q, QSqlDatabase db)
{
    static FicIdHash webIdToGlobal = GetGlobalIDHash(db, "").data;
    static FanficIdRecord result;
    auto ffn = q.value("ffn_id").toInt();
    auto ao3 = q.value("ao3_id").toInt();
    auto sb = q.value("sb_id").toInt();
    auto sv = q.value("sv_id").toInt();
    result.ids["ffn"] = ffn;
    result.ids["ao3"] = ao3;
    result.ids["sb"] = sb;
    result.ids["sv"] = sv;
    if(webIdToGlobal.ids.size() == 0)
        return result;

    if(webIdToGlobal.ids["ffn"].contains(ffn))
        result.ids["db"] = webIdToGlobal.ids["ffn"][ffn];
    else if(webIdToGlobal.ids["sb"].contains(sb))
        result.ids["db"] = webIdToGlobal.ids["sb"][sb];
    else if(webIdToGlobal.ids["sv"].contains(sv))
        result.ids["db"] = webIdToGlobal.ids["sv"][sv];
    else if(webIdToGlobal.ids["ao3"].contains(ao3))
        result.ids["db"] = webIdToGlobal.ids["ao3"][ao3];

    return result;
}

//bool ExecAndCheck(QSqlQuery& q)
//{
//    q.exec();
//    if(q.lastError().isValid())
//    {
//        if(q.lastError().text().contains("record"))
//            qDebug() << "Error while performing a query: ";
//        qDebug() << "Error while performing a query: ";
//        qDebug().noquote() << q.lastQuery();
//        qDebug() << "Error was: " <<  q.lastError();
//        qDebug() << q.lastError().nativeErrorCode();
//        if(q.lastError().text().contains("Parameter"))
//            return false;
//        return false;
//    }
//    return true;
//}
bool CheckExecution(QSqlQuery& q)
{
    if(q.lastError().isValid())
    {
        qDebug() << "SQLERROR: "<< q.lastError();
        qDebug() << q.lastQuery();
        return false;
    }
    return true;
}


#define SQLPARAMS
#define CTXA [&](auto ctx)
DiagnosticSQLResult<bool> SetFandomTracked(int id, bool tracked,  QSqlDatabase db)
{
    return SqlContext<bool> (db,
                             " UPDATE fandomindex SET tracked = :tracked where id = :id",
                             SQLPARAMS{{"tracked",QString(tracked ? "1" : "0")},
                                       {"id", id}})();
}

DiagnosticSQLResult<bool> CalculateFandomsFicCounts(QSqlDatabase db)
{
    QString qs = QString("update fandomindex set fic_count = (select count(fic_id) from ficfandoms where fandom_id = fandoms.id)");
    return SqlContext<bool> (db, qs)();
}

DiagnosticSQLResult<bool> UpdateFandomStats(int fandomId, QSqlDatabase db)
{
    QString qs = QString("update fandomsources set fic_count = "
                         " (select count(fic_id) from ficfandoms where fandom_id = :fandom_id)"
                         //! todo
                         //" average_faves_top_3 = (select sum(favourites)/3 from fanfics f where f.fandom = fandoms.fandom and f.id "
                         //" in (select id from fanfics where fanfics.fandom = fandoms.fandom order by favourites desc limit 3))"
                         " where fandoms.id = :fandom_id");

    SqlContext<bool> ctx(db, qs, {{"fandom_id",fandomId}});
    if(fandomId == -1)
        return ctx.result;
    return ctx();
}

DiagnosticSQLResult<bool> WriteMaxUpdateDateForFandom(QSharedPointer<core::Fandom> fandom, QSqlDatabase db)
{
    auto result = Internal::WriteMaxUpdateDateForFandom(fandom, "having count(*) = 1", db,
                                                        [](QSharedPointer<core::Fandom> f, QDateTime dt){
            f->lastUpdateDate = dt.date();
});
    return result;
}



DiagnosticSQLResult<bool>  Internal::WriteMaxUpdateDateForFandom(QSharedPointer<core::Fandom> fandom,
                                                                 QString condition,
                                                                 QSqlDatabase db,
                                                                 std::function<void(QSharedPointer<core::Fandom>,QDateTime)> writer                                           )
{
    QString qs = QString("select max(updated) as updated from fanfics where id in (select distinct fic_id from FicFandoms where fandom_id = :fandom_id %1");
    qs=qs.arg(condition);

    DiagnosticSQLResult<bool> opResult;
    SqlContext<QString> ctx(db, qs);
    ctx.FetchSingleValue<QString>("updated", "");
    opResult.success = ctx.result.success;
    if(!opResult.success)
        return opResult;

    QDateTime result;
    result = QDateTime::fromString(ctx.result.data, "yyyy-MM-ddThh:mm:ss.000");
    writer(fandom, result);
    return opResult;
}
//! todo  requires refactor. fandoms need to have a tag table attached to them instead of forced sections
DiagnosticSQLResult<QStringList> GetFandomListFromDB(QSqlDatabase db)
{
    QString qs = QString("select name from fandomindex");

    SqlContext<QStringList> ctx(db, qs);
    ctx.result.data.push_back("");
    ctx.FetchLargeSelectIntoList<QString>("name", qs);
    return ctx.result;
}

DiagnosticSQLResult<bool> AssignTagToFandom(QString tag, int fandom_id, QSqlDatabase db, bool includeCrossovers)
{
    QString qs = "INSERT INTO FicTags(fic_id, tag) SELECT fic_id, '%1' as tag from FicFandoms f WHERE fandom_id = :fandom_id "
                 " and NOT EXISTS(SELECT 1 FROM FicTags WHERE fic_id = f.fic_id and tag = '%1')";
    if(!includeCrossovers)
        qs+=" and (select count(distinct fandom_id) from ficfandoms where fic_id = f.fic_id) = 1";
    qs=qs.arg(tag);
    SqlContext<bool> ctx(db, qs, {{"fandom_id", fandom_id}});
    ctx.ExecAndCheck(true);
    return ctx.result;
}



DiagnosticSQLResult<bool> AssignTagToFanfic(QString tag, int fic_id, QSqlDatabase db)
{
    QString qs = "INSERT INTO FicTags(fic_id, tag, added) values(:fic_id, :tag, date('now'))";
    SqlContext<bool> ctx(db, qs, {{"tag", tag},{"fic_id", fic_id}});
    ctx.ExecAndCheck(true);
    //    ctx.ReplaceQuery("update fanfics set hidden = 1 where id = :fic_id");
    //    ctx.bindValue("fic_id", fic_id);
    //    ctx.ExecAndCheck(true);
    return ctx.result;
}


DiagnosticSQLResult<bool> RemoveTagFromFanfic(QString tag, int fic_id, QSqlDatabase db)
{
    QString qs = "delete from FicTags where fic_id = :fic_id and tag = :tag";
    SqlContext<bool> ctx (db, qs, {{"tag", tag},{"fic_id", fic_id}});
    ctx();

    //TODO need to  fix this... probably
//    ctx.ReplaceQuery("update fanfics set hidden = case "
//                     " when (select count(fic_id) from fictags where fic_id = :fic_id) > 0 then 1 "
//                     " else 0 end "
//                     " where id = :fic_id_");
//    ctx.bindValue("fic_id", fic_id);
//    ctx.bindValue("fic_id_", fic_id);
//    ctx.ExecAndCheck();
    return ctx.result;
}

DiagnosticSQLResult<bool> AssignSlashToFanfic(int fic_id, int source, QSqlDatabase db)
{
    QString qs = QString("update fanfics set slash_probability = 1, slash_source = :source where id = :fic_id");
    return SqlContext<bool> (db, qs, {{"source", source},{"fic_id", fic_id}})();
}

DiagnosticSQLResult<bool> AssignQueuedToFanfic(int fic_id, QSqlDatabase db)
{
    QString qs = QString("update fanfics set queued_for_action = 1 where id = :fic_id");
    return SqlContext<bool> (db, qs, {{"fic_id", fic_id}})();
}

DiagnosticSQLResult<bool> AssignChapterToFanfic(int fic_id, int chapter, QSqlDatabase db)
{
    QString qs = "INSERT INTO FicReadingTracker(fic_id, at_chapter) values(:fic_id, :at_chapter) "
                 "on conflict (fic_id) do update set at_chapter = :at_chapter_ where fic_id = :fic_id_";
    SqlContext<bool> ctx(db, qs);
    ctx.bindValue("fic_id", fic_id);
    ctx.bindValue("at_chapter", chapter);
    ctx.bindValue("at_chapter_", chapter);
    ctx.bindValue("fic_id_", fic_id);
    ctx.ExecAndCheck(true);
    return ctx.result;
}
DiagnosticSQLResult<bool> AssignScoreToFanfic(int score, int fic_id, QSqlDatabase db)
{
    if(score > 0)
    {
        QString qs = "INSERT INTO FicSCores(fic_id, score) values(:fic_id, :score) on conflict (fic_id) do update set score = :score where fic_id = :fic_id";
        SqlContext<bool> ctx(db, qs, BP4(fic_id, score, score, fic_id));
        ctx.ExecAndCheck(true);
        return ctx.result;
    }
    else {
        QString qs = QString(" delete from FicScores where fic_id  = :fic_id");
        return SqlContext<bool>(db, qs, BP1(fic_id))();
    }
}

DiagnosticSQLResult<int> GetLastFandomID(QSqlDatabase db){
    QString qs = QString("Select max(id) as maxid from fandomindex");

    SqlContext<int> ctx(db, qs);
    //qDebug() << "Db open: " << db.isOpen() << " " << db.connectionName();
    ctx.FetchSingleValue<int>("maxid", -1, true, qs);
    return ctx.result;
}

DiagnosticSQLResult<bool> WriteFandomUrls(core::FandomPtr fandom, QSqlDatabase db)
{
    QString qs = QString("insert into fandomurls(global_id, url, website, custom) values(:id, :url, :website, :custom)");

    SqlContext<bool> ctx(db, qs);
    if(fandom->urls.size() == 0)
        fandom->urls.push_back(core::Url("", "ffn"));
    ctx.ExecuteWithKeyListAndBindFunctor<core::Url>(fandom->urls, [&](core::Url& url, QSqlQuery& q){
        q.bindValue(":id", fandom->id);
        q.bindValue(":url", url.GetUrl());
        q.bindValue(":website", fandom->source);
        q.bindValue(":custom", fandom->section);
    }, true);
    return ctx.result;
}

DiagnosticSQLResult<bool> CreateFandomInDatabase(core::FandomPtr fandom, QSqlDatabase db, bool writeUrls, bool useSuppliedIds)
{
    SqlContext<bool> ctx(db);


    int newFandomId;
    if(useSuppliedIds)
        newFandomId  = fandom->id;
    else
    {
        auto lastId = GetLastFandomID(db);

        if(!lastId.success)
            ctx.result.success = false;
        newFandomId = lastId.data + 1;
    }
    if(newFandomId == -1)
        return ctx.result;

    QString qs = QString("insert into fandomindex(id, name) "
                         " values(:id, :name)");
    ctx.ReplaceQuery(qs);
    ctx.bindValue("name", fandom->GetName());
    ctx.bindValue("id", newFandomId);
    //qDebug() << db.isOpen();
    if(!ctx.ExecAndCheck())
        return ctx.result;

    qDebug() << "new fandom: " << fandom->GetName();

    fandom->id = newFandomId;
    if(writeUrls)
        ctx.result.success = WriteFandomUrls(fandom, db).success;
    return ctx.result;

}
DiagnosticSQLResult<int> GetFicIdByAuthorAndName(QString author, QString title, QSqlDatabase db)
{
    QString qs = " select id from fanfics where author = :author and title = :title";

    SqlContext<int> ctx(db, qs, {{"author", author},{"title", title}});
    ctx.FetchSingleValue<int>("id", -1);
    return ctx.result;
}

DiagnosticSQLResult<int> GetFicIdByWebId(QString website, int webId, QSqlDatabase db)
{
    QString qs = " select id from fanfics where %1_id = :site_id";
    qs = qs.arg(website);

    SqlContext<int> ctx(db, qs, {{"site_id",webId}});
    ctx.FetchSingleValue<int>("id", -1);
    return ctx.result;
}

core::FicPtr LoadFicFromQuery(QSqlQuery& q1, QString website = "ffn")
{
    auto fic = core::Fic::NewFanfic();
    fic->atChapter = q1.value("AT_CHAPTER").toInt();
    fic->complete  = q1.value("COMPLETE").toInt();
    fic->webId     = q1.value(website + "_ID").toInt();
    fic->id        = q1.value("ID").toInt();
    fic->wordCount = q1.value("WORDCOUNT").toString();
    //fic->chapters = q1.value("CHAPTERS").toString();
    fic->reviews = q1.value("REVIEWS").toString();
    fic->favourites = q1.value("FAVOURITES").toString();
    fic->follows = q1.value("FOLLOWS").toString();
    fic->rated = q1.value("RATED").toString();
    fic->fandom = q1.value("FANDOM").toString();
    fic->title = q1.value("TITLE").toString();
    fic->genres = q1.value("GENRES").toString().split("##");
    fic->summary = q1.value("SUMMARY").toString();
    fic->published = q1.value("PUBLISHED").toDateTime();
    fic->updated = q1.value("UPDATED").toDateTime();
    fic->characters = q1.value("CHARACTERS").toString().split(",");
    //fic->authorId = q1.value("AUTHOR_ID").toInt();
    fic->author->name = q1.value("AUTHOR").toString();
    fic->ffn_id = q1.value("FFN_ID").toInt();
    fic->ao3_id = q1.value("AO3_ID").toInt();
    fic->sb_id = q1.value("SB_ID").toInt();
    fic->sv_id = q1.value("SV_ID").toInt();
    return fic;
}

DiagnosticSQLResult<core::FicPtr> GetFicByWebId(QString website, int webId, QSqlDatabase db)
{
    QString qs = QString(" select * from fanfics where %1_id = :site_id").arg(website);
    SqlContext<core::FicPtr> ctx(db, qs, {{"site_id",webId}});
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data =  LoadFicFromQuery(q);
        ctx.result.data->webSite = website;
    });
    return ctx.result;
}

DiagnosticSQLResult<core::FicPtr> GetFicById( int ficId, QSqlDatabase db)
{
    QString qs = " select * from fanfics where id = :fic_id";
    SqlContext<core::FicPtr> ctx(db, qs, {{"fic_id",ficId}});
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data =  LoadFicFromQuery(q);
    });
    return ctx.result;
}


DiagnosticSQLResult<bool> SetUpdateOrInsert(QSharedPointer<core::Fic> fic, QSqlDatabase db, bool alwaysUpdateIfNotInsert)
{
    QString getKeyQuery = QString("Select ( select count(*) from FANFICS where  %1_id = :site_id1) as COUNT_NAMED,"
                                  " ( select count(*) from FANFICS where  %1_id = :site_id2 "
                                  "and (updated < :updated or updated is null)) as count_updated").arg(fic->webSite);

    SqlContext<bool> ctx(db, getKeyQuery);
    ctx.bindValue("site_id1", fic->webId);
    ctx.bindValue("site_id2", fic->webId);
    ctx.bindValue("updated", fic->updated);
    if(!fic)
        return ctx.result;

    int countNamed = 0;
    int countUpdated = 0;
    ctx.ForEachInSelect([&](QSqlQuery& q){
        countNamed = q.value("COUNT_NAMED").toInt();
        countUpdated = q.value("count_updated").toInt();
    });
    if(!ctx.Success())
        return ctx.result;

    bool requiresInsert = countNamed == 0;
    bool requiresUpdate = countUpdated > 0;
//    if(fic->fandom.contains("Greatest Showman"))
//        qDebug() << fic->fandom;
    if(alwaysUpdateIfNotInsert || (!requiresInsert && requiresUpdate))
        fic->updateMode = core::UpdateMode::update;
    if(requiresInsert)
        fic->updateMode = core::UpdateMode::insert;

    return ctx.result;
}

DiagnosticSQLResult<bool> InsertIntoDB(QSharedPointer<core::Fic> section, QSqlDatabase db)
{
    QString query = "INSERT INTO FANFICS (%1_id, FANDOM, AUTHOR, TITLE,WORDCOUNT, CHAPTERS, FAVOURITES, REVIEWS, "
                    " CHARACTERS, COMPLETE, RATED, SUMMARY, GENRES, PUBLISHED, UPDATED, AUTHOR_ID,"
                    " wcr, reviewstofavourites, age, daysrunning, at_chapter, lastupdate, fandom1, fandom2, author_id ) "
                    "VALUES ( :site_id,  :fandom, :author, :title, :wordcount, :CHAPTERS, :FAVOURITES, :REVIEWS, "
                    " :CHARACTERS, :COMPLETE, :RATED, :summary, :genres, :published, :updated, :author_id,"
                    " :wcr, :reviewstofavourites, :age, :daysrunning, 0, date('now'), :fandom1, :fandom2, :author_id)";
    query=query.arg(section->webSite);

    SqlContext<bool> ctx(db, query);
    ctx.bindValue("site_id",section->webId); //?
    ctx.bindValue("fandom",section->fandom);
    ctx.bindValue("author",section->author->name); //?
    ctx.bindValue("author_id",section->author->GetWebID("ffn"));
    ctx.bindValue("title",section->title);
    ctx.bindValue("wordcount",section->wordCount.toInt());
    ctx.bindValue("CHAPTERS",section->chapters.trimmed().toInt());
    ctx.bindValue("FAVOURITES",section->favourites.toInt());
    ctx.bindValue("REVIEWS",section->reviews.toInt());
    ctx.bindValue("CHARACTERS",section->charactersFull);
    ctx.bindValue("RATED",section->rated);
    ctx.bindValue("summary",section->summary);
    ctx.bindValue("COMPLETE",section->complete);
    ctx.bindValue("genres",section->genreString);
    ctx.bindValue("published",section->published);
    ctx.bindValue("updated",section->updated);
    ctx.bindValue("wcr",section->calcStats.wcr);
    ctx.bindValue("reviewstofavourites",section->calcStats.reviewsTofavourites);
    ctx.bindValue("age",section->calcStats.age);
    ctx.bindValue("daysrunning",section->calcStats.daysRunning);
    if(section->fandomIds.size() > 0)
        ctx.bindValue("fandom1",section->fandomIds.at(0));
    else
        ctx.bindValue("fandom1",-1);
    if(section->fandomIds.size() > 1)
        ctx.bindValue("fandom2",section->fandomIds.at(1));
    else
        ctx.bindValue("fandom2",-1);

    return ctx();
}
DiagnosticSQLResult<bool>  UpdateInDB(QSharedPointer<core::Fic> section, QSqlDatabase db)
{
    QString query = "UPDATE FANFICS set fandom = :fandom, wordcount= :wordcount, CHAPTERS = :CHAPTERS,  "
                    "COMPLETE = :COMPLETE, FAVOURITES = :FAVOURITES, REVIEWS= :REVIEWS, CHARACTERS = :CHARACTERS, RATED = :RATED, "
                    "summary = :summary, genres= :genres, published = :published, updated = :updated, author_id = :author_id,"
                    "wcr= :wcr,  author= :author, title= :title, reviewstofavourites = :reviewstofavourites, "
                    "age = :age, daysrunning = :daysrunning, lastupdate = date('now'),"
                    " fandom1 = :fandom1, fandom2 = :fandom2 "
                    " where %1_id = :site_id";
    query=query.arg(section->webSite);
    SqlContext<bool> ctx(db, query);
    ctx.bindValue("fandom",section->fandom);
    ctx.bindValue("author",section->author->name);
    ctx.bindValue("author_id",section->author->GetWebID("ffn"));
    ctx.bindValue("title",section->title);
    ctx.bindValue("wordcount",section->wordCount.toInt());
    ctx.bindValue("CHAPTERS",section->chapters.trimmed().toInt());
    ctx.bindValue("FAVOURITES",section->favourites.toInt());
    ctx.bindValue("REVIEWS",section->reviews.toInt());
    ctx.bindValue("CHARACTERS",section->charactersFull);
    ctx.bindValue("RATED",section->rated);
    ctx.bindValue("summary",section->summary);
    ctx.bindValue("COMPLETE",section->complete);
    ctx.bindValue("genres",section->genreString);
    ctx.bindValue("published",section->published);
    ctx.bindValue("updated",section->updated);
    ctx.bindValue("site_id",section->webId);
    ctx.bindValue("wcr",section->calcStats.wcr);
    ctx.bindValue("reviewstofavourites",section->calcStats.reviewsTofavourites);
    ctx.bindValue("age",section->calcStats.age);
    ctx.bindValue("daysrunning",section->calcStats.daysRunning);
    if(section->fandomIds.size() > 0)
        ctx.bindValue("fandom1",section->fandomIds.at(0));
    else
        ctx.bindValue("fandom1",-1);
    if(section->fandomIds.size() > 1)
        ctx.bindValue("fandom2",section->fandomIds.at(1));
    else
        ctx.bindValue("fandom2",-1);
    //ctx.bindValue("author_id",section->author->GetWebID("ffn"));
    ctx.ExecAndCheck();
    return ctx.result;
}

DiagnosticSQLResult<bool> WriteRecommendation(core::AuthorPtr author, int fic_id, QSqlDatabase db)
{
    // atm this pairs favourite story with an author
    QString qs = " insert into recommendations (recommender_id, fic_id) values(:recommender_id,:fic_id); ";
    SqlContext<bool> ctx(db, qs, {{"recommender_id", author->id},{"fic_id", fic_id}});
    if(!author || author->id < 0)
        return ctx.result;

    return ctx(true);
}

DiagnosticSQLResult<bool> WriteFicRelations(QList<core::FicWeightResult> ficRelations, QSqlDatabase db)
{
    QString qs = " insert into FicRelations (fic1, fic2, fic1_list_count, fic2_list_count, meeting_list_count, same_fandom, attraction, repullsion, final_attraction)"
                 " values(:fic1, :fic2, :fic1_list_count, :fic2_list_count, :meeting_list_count, :same_fandom, :attraction, :repullsion, :final_attraction); ";
    SqlContext<bool> ctx(db);
    ctx.Prepare(qs);
    DiagnosticSQLResult<bool> result;
    for(auto token : ficRelations)
    {
        ctx.bindValue("fic1",token.ficId1);
        ctx.bindValue("fic2",token.ficId2);
        ctx.bindValue("fic1_list_count",token.ficListCount1);
        ctx.bindValue("fic2_list_count",token.ficListCount2);
        ctx.bindValue("meeting_list_count",token.meetingCount);
        ctx.bindValue("same_fandom",token.sameFandom);
        ctx.bindValue("attraction",token.attraction);
        ctx.bindValue("repullsion",token.repulsion);
        ctx.bindValue("final_attraction",token.finalAttraction);
        ctx();
        if(!ctx.result.success)
        {
            result.success = false;
            result.oracleError = ctx.result.oracleError;
            break;
        }
    }
    return result;
}

DiagnosticSQLResult<bool> WriteAuthorsForFics(QHash<uint32_t, uint32_t> data,  QSqlDatabase db)
{
    QString qs = " insert into FicAuthors (fic_id, author_id)"
                 " values(:fic_id, :author_id); ";
    SqlContext<bool> ctx(db);
    ctx.Prepare(qs);
    DiagnosticSQLResult<bool> result;
    for(auto fic: data.keys())
    {
        ctx.bindValue("fic_id",fic);
        ctx.bindValue("author_id",data[fic]);
        ctx();
        if(!ctx.result.success)
        {
            result.success = false;
            result.oracleError = ctx.result.oracleError;
            break;
        }
    }
    return result;
}


DiagnosticSQLResult<int>  GetAuthorIdFromUrl(QString url, QSqlDatabase db)
{
    QString qsl = " select id from recommenders where url like '%%1%' ";
    qsl = qsl.arg(url);

    SqlContext<int> ctx(db, qsl);
    ctx.FetchSingleValue<int>("id", -1);
    return ctx.result;
}
DiagnosticSQLResult<int>  GetAuthorIdFromWebID(int id, QString website, QSqlDatabase db)
{
    QString qs = "select id from recommenders where %1_id = :id";
    qs = qs.arg(website);

    SqlContext<int> ctx(db, qs, {{"id", id}});
    ctx.FetchSingleValue<int>("id", -1, false);
    return ctx.result;
}


DiagnosticSQLResult<QSet<int> > GetAuthorsForFics(QSet<int> fics, QSqlDatabase db)
{
    auto* userThreadData = ThreadData::GetUserData();
    userThreadData->ficsForAuthorSearch = fics;
    QString qs = "select distinct author_id from fanfics where cfInFicsForAuthors(id) > 0";
    SqlContext<QSet<int>> ctx(db, qs);
    ctx.FetchLargeSelectIntoList<int>("author_id", qs, "",[](QSqlQuery& q){
        return q.value("author_id").toInt();
    });
    ctx.result.data.remove(0);
    ctx.result.data.remove(-1);
    return ctx.result;

}
DiagnosticSQLResult<QSet<int>> GetRecommendersForFics(QSet<int> fics, QSqlDatabase db)
{
    QString qs = "select distinct recommender_id from  recommendations where fic_id in (%1)";
    QStringList list;
    for(auto fic: fics)
        list.push_back(QString::number(fic));
    qs = qs.arg("'" + list.join("','") + "'");
    SqlContext<QSet<int>> ctx(db, qs);
    ctx.FetchLargeSelectIntoList<int>("recommender_id", qs, "",[](QSqlQuery& q){
        return q.value("recommender_id").toInt();
    });
    ctx.result.data.remove(0);
    ctx.result.data.remove(-1);
    return ctx.result;
}

DiagnosticSQLResult<QHash<uint32_t, int>> GetHashAuthorsForFics(QSet<int> fics, QSqlDatabase db)
{
    auto* userThreadData = ThreadData::GetUserData();
    userThreadData->ficsForAuthorSearch = fics;
    QString qs = "select author_id, id from fanfics where cfInFicsForAuthors(id) > 0";
    SqlContext<QHash<uint32_t, int>> ctx(db, qs);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data[q.value("id").toUInt()] = q.value("author_id").toInt();
    });

    return ctx.result;
}

DiagnosticSQLResult<bool> AssignNewNameToAuthorWithId(core::AuthorPtr author, QSqlDatabase db)
{
    QString qs = " UPDATE recommenders SET name = :name where id = :id";
    SqlContext<bool> ctx(db, qs, {{"name",author->name}, {"id",author->id}});
    if(author->GetIdStatus() != core::AuthorIdStatus::valid)
        return ctx.result;
    return ctx();
}

void ProcessIdsFromQuery(core::AuthorPtr author, QSqlQuery& q)
{
    auto ffnId = q.value("ffn_id").toInt();
    auto ao3Id = q.value("ao3_id").toInt();
    auto sbId = q.value("sb_id").toInt();
    auto svId = q.value("sv_id").toInt();
    if(ffnId != -1)
        author->SetWebID("ffn", ffnId);
    if(ao3Id != -1)
        author->SetWebID("ao3", ao3Id);
    if(sbId != -1)
        author->SetWebID("sb", sbId);
    if(svId != -1)
        author->SetWebID("sv", svId);
}

core::AuthorPtr AuthorFromQuery(QSqlQuery& q)
{
    core::AuthorPtr result(new core::Author);
    result->AssignId(q.value("id").toInt());
    result->name = q.value("name").toString();
    result->recCount = q.value("rec_count").toInt();
    result->stats.favouritesLastUpdated = q.value("last_favourites_update").toDateTime().date();
    result->stats.favouritesLastChecked = q.value("last_favourites_checked").toDateTime().date();
    ProcessIdsFromQuery(result, q);
    return result;
}


DiagnosticSQLResult<QList<core::AuthorPtr>> GetAllAuthors(QString website,  QSqlDatabase db, int limit)
{
    QString qs = QString("select distinct id,name, url, ffn_id, ao3_id,sb_id, sv_id,  "
                         "(select count(fic_id) from recommendations where recommender_id = recommenders.id) as rec_count,"
                         " last_favourites_update, last_favourites_checked "
                         " from recommenders where website_type = :site order by id %1");
    if(limit > 0)
        qs = qs.arg(QString(" LIMIT %1 ").arg(QString::number(limit)));
    else
        qs = qs.arg("");

    //!!! bindvalue incorrect for first query?
    SqlContext<QList<core::AuthorPtr>> ctx(db, qs, {{"site",website}});
    ctx.FetchLargeSelectIntoList<core::AuthorPtr>("", qs, "select count(*) from recommenders where website_type = :site",[](QSqlQuery& q){
        return AuthorFromQuery(q);
    });
    return ctx.result;
}


DiagnosticSQLResult<QList<core::AuthorPtr>> GetAllAuthorsWithFavUpdateSince(QString website,
                                                                            QDateTime date,
                                                                            QSqlDatabase db,
                                                                            int limit)
{
    //todo fix reccount, needs to be precalculated in recommenders table

    QString qs = QString("select distinct id,name, url, "
                         "ffn_id, ao3_id,sb_id, sv_id, "
                         " last_favourites_update, last_favourites_checked, "
                         "(select count(fic_id) from recommendations where recommender_id = recommenders.id) as rec_count "
                         " from recommenders where website_type = :site "
                         " and last_favourites_update > :date "
                         "order by id %1");
    if(limit > 0)
        qs = qs.arg(QString(" LIMIT %1 ").arg(QString::number(limit)));
    else
        qs = qs.arg("");


    SqlContext<QList<core::AuthorPtr>> ctx(db, qs);
    ctx.bindValue("site",website);
    ctx.bindValue("date",date);
    ctx.FetchLargeSelectIntoList<core::AuthorPtr>("", qs,
                                                  "select count(*) from recommenders where website_type = :site",
                                                  [](QSqlQuery& q){
        return AuthorFromQuery(q);
    });


    return ctx.result;
}

DiagnosticSQLResult<QList<core::AuthorPtr>> GetAllAuthorsWithFavUpdateBetween(QString website,
                                                                             QDateTime dateStart,
                                                                             QDateTime dateEnd,
                                                                             QSqlDatabase db,
                                                                             int limit)
{
    //todo fix reccount, needs to be precalculated in recommenders table

    QString qs = QString("select distinct id,name, url, "
                         "ffn_id, ao3_id,sb_id, sv_id, "
                         " last_favourites_update, last_favourites_checked, "
                         "(select count(fic_id) from recommendations where recommender_id = recommenders.id) as rec_count "
                         " from recommenders where "
                         " last_favourites_update <= :date_start "
                         " and  last_favourites_update >= :date_end "
                         " and website_type = :site "
                         "order by id %1");
    if(limit > 0)
        qs = qs.arg(QString(" LIMIT %1 ").arg(QString::number(limit)));
    else
        qs = qs.arg("");

    qDebug() << "fething authors between " << dateEnd << " and " << dateStart;
    SqlContext<QList<core::AuthorPtr>> ctx(db, qs);
    ctx.bindValue("date_start",dateStart);
    ctx.bindValue("date_end",dateEnd);
    ctx.bindValue("site",website);
    ctx.FetchLargeSelectIntoList<core::AuthorPtr>("", qs,
                                                  "select count(*) from recommenders where website_type = :site",
                                                  [](QSqlQuery& q){
        return AuthorFromQuery(q);
    });


    return ctx.result;
}



DiagnosticSQLResult<QList<core::AuthorPtr>> GetAuthorsForRecommendationList(int listId,  QSqlDatabase db)
{
    QString qs = QString("select id,name, url, ffn_id, ao3_id,sb_id, sv_id, "
                         "(select count(fic_id) from recommendations where recommender_id = recommenders.id) as rec_count, "
                         " last_favourites_update, last_favourites_checked "
                         " from recommenders "
                         "where id in ( select author_id from RecommendationListAuthorStats where list_id = :list_id )");

    SqlContext<QList<core::AuthorPtr>> ctx(db, qs, {{"list_id",listId}});
    ctx.ForEachInSelect([&](QSqlQuery& q){
        auto author = AuthorFromQuery(q);
        ctx.result.data.push_back(author);
    });
    return ctx.result;
}

DiagnosticSQLResult<QString> GetAuthorsForRecommendationListClient(int list_id,  QSqlDatabase db)
{
    QString qs = "select sources from recommendationlists where id = :list_id";
    SqlContext<QString> ctx(db, qs, BP1(list_id));
    ctx.FetchSingleValue<QString>("sources", "");
    return ctx.result;
}


DiagnosticSQLResult<core::AuthorPtr> GetAuthorByNameAndWebsite(QString name, QString website, QSqlDatabase db)
{
    core::AuthorPtr result;
    QString qs = QString("select id,"
                         "name, url, website_type, ffn_id, ao3_id,sb_id, sv_id,"
                         " last_favourites_update, last_favourites_checked "
                         " from recommenders where %1_id is not null and name = :name");
    qs=qs.arg(website);

    SqlContext<core::AuthorPtr> ctx(db, qs, {{"site",website},{"name",name}});
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data = AuthorFromQuery(q);
    });
    return ctx.result;
}
DiagnosticSQLResult<core::AuthorPtr> GetAuthorByIDAndWebsite(int id, QString website, QSqlDatabase db)
{
    QString qs = QString("select r.id,r.name, r.url, r.website_type, r.ffn_id, r.ao3_id,r.sb_id, r.sv_id, "
                         " (select count(fic_id) from recommendations where recommender_id = r.id) as rec_count,"
                         " last_favourites_update, last_favourites_checked "
                         "from recommenders r where %1_id is not null and %1_id = :id");
    qs=qs.arg(website);
    SqlContext<core::AuthorPtr> ctx(db, qs, {{"site",website},{"id",id}});
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data = AuthorFromQuery(q);
    });
    return ctx.result;
}

void AuthorStatisticsFromQuery(QSqlQuery& q,  core::AuthorPtr author)
{
    core::FicSectionStats& stats = author->stats.favouriteStats;
    stats.favourites = q.value("favourites").toInt();
    stats.ficWordCount = q.value("favourites_wordcount").toInt();
    stats.averageLength = q.value("average_words_per_chapter").toInt();
    stats.esrbType = static_cast<core::FicSectionStats::ESRBType>(q.value("esrb_type").toInt());
    stats.prevalentMood = static_cast<core::FicSectionStats::MoodType>(q.value("prevalent_mood").toInt());
    stats.mostFavouritedSize = static_cast<core::EntitySizeType>(q.value("most_favourited_size").toInt());
    stats.sectionRelativeSize= static_cast<core::EntitySizeType>(q.value("favourites_type").toInt());
    stats.averageLength = q.value("average_favourited_length").toDouble();
    stats.fandomsDiversity = q.value("favourite_fandoms_diversity").toDouble();

    stats.explorerFactor = q.value("explorer_factor").toDouble();
    stats.megaExplorerFactor= q.value("mega_explorer_factor").toDouble();

    stats.crossoverFactor  = q.value("crossover_factor").toDouble();
    stats.unfinishedFactor = q.value("unfinished_factor").toDouble();

    stats.esrbUniformityFactor = q.value("esrb_uniformity_factor").toDouble();
    stats.esrbKiddy= q.value("esrb_kiddy").toDouble();
    stats.esrbMature= q.value("esrb_mature").toDouble();

    stats.genreDiversityFactor = q.value("genre_diversity_factor").toDouble();
    stats.moodUniformity = q.value("mood_uniformity_factor").toDouble();

    stats.moodSad= q.value("mood_sad").toDouble();
    stats.moodNeutral= q.value("mood_neutral").toDouble();
    stats.moodHappy= q.value("mood_happy").toDouble();

    stats.crackRatio= q.value("crack_factor").toDouble();
    stats.smutRatio= q.value("smut_factor").toDouble();
    stats.slashRatio= q.value("slash_factor").toDouble();
    stats.notSlashRatio= q.value("not_slash_factor").toDouble();
    stats.prevalentGenre = q.value("prevalent_genre").toString();


    stats.sizeFactors[0] = q.value("size_tiny").toDouble();
    stats.sizeFactors[1] =  q.value("size_medium").toDouble();
    stats.sizeFactors[2] =  q.value("size_large").toDouble();
    stats.sizeFactors[3] = q.value("size_huge").toDouble();
    stats.firstPublished = q.value("first_published").toDate();
    stats.lastPublished= q.value("last_published").toDate();
}

DiagnosticSQLResult<bool> LoadAuthorStatistics(core::AuthorPtr author, QSqlDatabase db)
{
    QString qs = QString("select * from AuthorFavouritesStatistics  where author_id = :id");

    SqlContext<bool> ctx(db, qs, {{"id",author->id}});
    ctx.ForEachInSelect([&](QSqlQuery& q){
        AuthorStatisticsFromQuery(q, author);
    });
    return ctx.result;
}

DiagnosticSQLResult<QHash<int, QSet<int>>> LoadFullFavouritesHashset(QSqlDatabase db)
{
    QString qs = QString("select * from recommendations order by recommender_id");

    SqlContext<QHash<int, QSet<int>>> ctx(db, qs);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data[q.value("recommender_id").toInt()].insert(q.value("fic_id").toInt());
    });
    return ctx.result;
}

DiagnosticSQLResult<core::AuthorPtr> GetAuthorByUrl(QString url, QSqlDatabase db)
{
    QString qs = QString("select r.id,name, r.url, r.ffn_id, r.ao3_id, r.sb_id, r.sv_id, "
                         " (select count(fic_id) from recommendations where recommender_id = r.id) as rec_count,"
                         " last_favourites_update, last_favourites_checked "
                         " from recommenders r where url = :url");

    SqlContext<core::AuthorPtr> ctx(db, qs, {{"url",url}});
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data = AuthorFromQuery(q);
    });
    return ctx.result;
}

DiagnosticSQLResult<core::AuthorPtr> GetAuthorById(int id, QSqlDatabase db)
{
    QString qs = QString("select id,name, url, ffn_id, ao3_id,sb_id, sv_id, "
                         "(select count(fic_id) from recommendations where recommender_id = :id) as rec_count, "
                         " last_favourites_update, last_favourites_checked "
                         "from recommenders where id = :id");

    SqlContext<core::AuthorPtr> ctx(db, qs, {{"id",id}});
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data = AuthorFromQuery(q);
    });
    return ctx.result;
}
DiagnosticSQLResult<bool> AssignAuthorNamesForWebIDsInFanficTable(QSqlDatabase db){

    QString qs = " UPDATE fanfics SET author = (select name from recommenders rs where rs.ffn_id = fanfics.author_id) where exists (select name from recommenders rs where rs.ffn_id = fanfics.author_id)";
    SqlContext<bool> ctx(db, qs);
    return ctx();
}
DiagnosticSQLResult<QList<QSharedPointer<core::RecommendationList>>> GetAvailableRecommendationLists(QSqlDatabase db)
{
    QString qs = QString("select * from RecommendationLists order by name");
    SqlContext<QList<QSharedPointer<core::RecommendationList>>> ctx(db, qs);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        QSharedPointer<core::RecommendationList>  list(new core::RecommendationList);
        list->alwaysPickAt = q.value("always_pick_at").toInt();
        list->minimumMatch = q.value("minimum").toInt();
        list->maxUnmatchedPerMatch= q.value("pick_ratio").toDouble();
        list->id= q.value("id").toInt();
        list->name= q.value("name").toString();
        list->ficCount= q.value("fic_count").toInt();
        ctx.result.data.push_back(list);
    });
    return ctx.result;

}
// LIMIT
DiagnosticSQLResult<QSharedPointer<core::RecommendationList>> GetRecommendationList(int listId, QSqlDatabase db)
{
    QString qs = QString("select * from RecommendationLists where id = :list_id");
    SqlContext<QSharedPointer<core::RecommendationList>> ctx(db, qs, {{"list_id", listId}});
    ctx.ForEachInSelect([&](QSqlQuery& q){
        QSharedPointer<core::RecommendationList>  list(new core::RecommendationList);
        list->alwaysPickAt = q.value("always_pick_at").toInt();
        list->minimumMatch = q.value("minimum").toInt();
        list->maxUnmatchedPerMatch= q.value("pick_ratio").toDouble();
        list->id= q.value("id").toInt();
        list->name= q.value("name").toString();
        list->ficCount= q.value("fic_count").toInt();
        ctx.result.data = list;
    });
    return ctx.result;
}

DiagnosticSQLResult<QSharedPointer<core::RecommendationList>> GetRecommendationList(QString name, QSqlDatabase db)
{
    QString qs = QString("select * from RecommendationLists where name = :list_name");

    SqlContext<QSharedPointer<core::RecommendationList>> ctx(db, qs, {{"list_name", name}});
    ctx.ForEachInSelect([&](QSqlQuery& q){
        QSharedPointer<core::RecommendationList>  list(new core::RecommendationList);
        list->alwaysPickAt = q.value("always_pick_at").toInt();
        list->minimumMatch = q.value("minimum").toInt();
        list->maxUnmatchedPerMatch= q.value("pick_ratio").toDouble();
        list->id= q.value("id").toInt();
        list->name= q.value("name").toString();
        list->ficCount= q.value("fic_count").toInt();
        ctx.result.data = list;
    });
    return ctx.result;
}

DiagnosticSQLResult<QList<core::AuhtorStatsPtr>> GetRecommenderStatsForList(int listId, QString sortOn, QString order, QSqlDatabase db)
{
    QString qs = QString("select rts.match_count as match_count,"
                         "rts.match_ratio as match_ratio,"
                         "rts.author_id as author_id,"
                         "rts.fic_count as fic_count,"
                         "r.name as name"
                         "  from RecommendationListAuthorStats rts, recommenders r "
                         " where rts.author_id = r.id and list_id = :list_id order by %1 %2");
    qs=qs.arg(sortOn).arg(order);
    SqlContext<QList<core::AuhtorStatsPtr>> ctx(db, qs, {{"list_id", listId}});
    ctx.ForEachInSelect([&](QSqlQuery& q){
        core::AuhtorStatsPtr stats(new core::AuthorRecommendationStats);
        stats->isValid = true;
        stats->matchesWithReference = q.value("match_count").toInt();
        stats->matchRatio= q.value("match_ratio").toDouble();
        stats->authorId= q.value("author_id").toInt();
        stats->listId= listId;
        stats->totalRecommendations= q.value("fic_count").toInt();
        stats->authorName = q.value("name").toString();
        ctx.result.data.push_back(stats);
    });
    return ctx.result;

}

DiagnosticSQLResult<int> GetMatchCountForRecommenderOnList(int authorId, int list, QSqlDatabase db)
{
    QString qs = "select fic_count from RecommendationListAuthorStats where list_id = :list_id and author_id = :author_id";
    SqlContext<int> ctx(db, qs, {{"list_id", list}, {"author_id",authorId}});
    ctx.FetchSingleValue<int>("fic_count", -1);
    return ctx.result;
}

DiagnosticSQLResult<QVector<int>> GetAllFicIDsFromRecommendationList(int listId,  core::StoryFilter::ESourceListLimiter limiter, QSqlDatabase db)
{
    QString qs = QString("select fic_id from RecommendationListData where list_id = :list_id");

    if(limiter == core::StoryFilter::sll_above_average)
        qs+=" and (votes_uncommon > 0 or votes_rare > 0 or votes_unique> 0)";
    else if(limiter == core::StoryFilter::sll_very_close)
        qs+=" and (votes_rare > 0 or votes_unique > 0)";
    if(limiter == core::StoryFilter::sll_exceptional)
        qs+=" and (votes_unique > 0)";
    SqlContext<QVector<int>> ctx(db);
    ctx.bindValue("list_id",listId);
    ctx.FetchLargeSelectIntoList<int>("fic_id", qs);
    return ctx.result;
}

DiagnosticSQLResult<QVector<int>> GetAllSourceFicIDsFromRecommendationList(int listId,  QSqlDatabase db)
{
    QString qs = QString("select fic_id from RecommendationListData where list_id = :list_id and is_origin = 1");
    SqlContext<QVector<int>> ctx(db);
    ctx.bindValue("list_id",listId);
    ctx.FetchLargeSelectIntoList<int>("fic_id", qs);
    return ctx.result;
}




DiagnosticSQLResult<QHash<int,int>> GetAllFicsHashFromRecommendationList(int listId,
                                                                         QSqlDatabase db,
                                                                         int minMatchCount,
                                                                         core::StoryFilter::ESourceListLimiter limiter, bool displayPurged)
{
    QString qs = QString("select fic_id, match_count from RecommendationListData where list_id = :list_id");
    if(minMatchCount != 0)
        qs += " and match_count > :match_count";

    if(limiter == core::StoryFilter::sll_above_average)
        qs+=" and (votes_uncommon > 0 or votes_rare > 0 or votes_unique> 0)";
    else if(limiter == core::StoryFilter::sll_very_close)
        qs+=" and (votes_rare > 0 or votes_unique > 0)";
    if(limiter == core::StoryFilter::sll_exceptional)
        qs+=" and (votes_unique > 0)";

    if(!displayPurged)
        qs+=" and (purged = 0)";

    SqlContext<QHash<int,int>> ctx(db, qs);
    ctx.bindValue("list_id",listId);
    if(minMatchCount != 0)
        ctx.bindValue("match_count",minMatchCount);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data[q.value("fic_id").toInt()] = q.value("match_count").toInt();
    });
    return ctx.result;
}

DiagnosticSQLResult<QStringList> GetAllAuthorNamesForRecommendationList(int listId, QSqlDatabase db)
{
    QString qs = QString("select name from recommenders where id in (select author_id from RecommendationListAuthorStats where list_id = :list_id)");
    SqlContext<QStringList> ctx(db);
    ctx.bindValue("list_id",listId);
    ctx.FetchLargeSelectIntoList<QString>("name", qs);
    return ctx.result;
}

DiagnosticSQLResult<int>  GetCountOfTagInAuthorRecommendations(int authorId, QString tag, QSqlDatabase db)
{
    QString qs = QString("select count(distinct fic_id) as ficcount from FicTags ft where ft.tag = :tag and exists"
                         " (select 1 from Recommendations where ft.fic_id = fic_id and recommender_id = :recommender_id)");

    SqlContext<int> ctx(db, qs, {{"tag", tag}, {"recommender_id",authorId}});
    ctx.FetchSingleValue<int>("ficcount", 0);
    return ctx.result;
}

//!todo needs check if the query actually works
DiagnosticSQLResult<int> GetMatchesWithListIdInAuthorRecommendations(int authorId, int listId, QSqlDatabase db)
{
    QString qs = QString("select count(fic_id) as ficcount from Recommendations r where recommender_id = :author_id and exists "
                         " (select 1 from RecommendationListData rld where rld.list_id = :list_id and fic_id = rld.fic_id)");
    SqlContext<int> ctx(db, qs, {{"author_id", authorId}, {"list_id",listId}});
    ctx.FetchSingleValue<int>("ficcount", 0);
    return ctx.result;
}

DiagnosticSQLResult<bool> DeleteRecommendationList(int listId, QSqlDatabase db )
{
    SqlContext<bool> ctx(db);
    if(listId == 0)
        return ctx.result;
    ctx.bindValue("list_id", listId);
    ctx.ExecuteList({"delete from RecommendationLists where id = :list_id",
                     "delete from RecommendationListAuthorStats where list_id = :list_id",
                     "delete from RecommendationListData where list_id = :list_id"});

    return ctx.result;
}

DiagnosticSQLResult<bool> DeleteRecommendationListData(int listId, QSqlDatabase db)
{
    SqlContext<bool> ctx(db);
    if(listId == 0)
        return ctx.result;
    ctx.bindValue("list_id", listId);
    ctx.ExecuteList({"delete from RecommendationListAuthorStats where list_id = :list_id",
                     "delete from RecommendationListData where list_id = :list_id"});

    return ctx.result;
}

DiagnosticSQLResult<bool> CopyAllAuthorRecommendationsToList(int authorId, int listId, QSqlDatabase db )
{
    QString qs = QString("insert into RecommendationListData (fic_id, list_id)"
                         " select fic_id, %1 as list_id from recommendations r where r.recommender_id = :author_id and "
                         " not exists( select 1 from RecommendationListData where list_id=:list_id and fic_id = r.fic_id) ");
    qs=qs.arg(listId);
    return SqlContext<bool>(db, qs, {{"author_id",authorId},{"list_id",listId}})();
}
DiagnosticSQLResult<bool> WriteAuthorRecommendationStatsForList(int listId, core::AuhtorStatsPtr stats, QSqlDatabase db)
{
    QString qs = QString("insert into RecommendationListAuthorStats (author_id, fic_count, match_count, match_ratio, list_id) "
                         "values(:author_id, :fic_count, :match_count, :match_ratio, :list_id)");

    SqlContext<bool> ctx(db, qs);
    if(!stats)
        return ctx.result;
    ctx.bindValue("author_id",stats->authorId);
    ctx.bindValue("fic_count",stats->totalRecommendations);
    ctx.bindValue("match_count",stats->matchesWithReference);
    ctx.bindValue("match_ratio",stats->matchRatio);
    ctx.bindValue("list_id",listId);
    ctx.ExecAndCheck(true);
    return ctx.result;
}
DiagnosticSQLResult<bool> CreateOrUpdateRecommendationList(QSharedPointer<core::RecommendationList> list, QDateTime creationTimestamp, QSqlDatabase db)
{
    QString qs;
    SqlContext<bool> ctx(db, qs);
    //int freeId = -2;
    {
        qs = QString("select max(id) as id from RecommendationLists");
        ctx.ReplaceQuery(qs);
        if(!ctx.ExecAndCheckForData())
        {
            return ctx.result;
        }
        //freeId = ctx.value("id").toInt() + 1;
        qDebug() << "At this moment max id is: " << ctx.value("id").toInt();
    }
    qDebug() << "List's name is: " << list->name;
    qs = QString("insert into RecommendationLists(name) select '%1' "
                         " where not exists(select 1 from RecommendationLists where name = '%1')").arg(list->name);
    ctx.ReplaceQuery(qs);
    if(!ctx.ExecAndCheck())
    {
        list->id = -1;
        return ctx.result;
    }
    qs = QString("select id from RecommendationLists where name = :name");
    SqlContext<bool> ctx2(db, qs);
    ctx2.bindValue("name",list->name);
    if(!ctx2.ExecAndCheckForData())
    {
        list->id = -1;
        return ctx2.result;
    }
    list->id = ctx2.value("id").toInt();
    qDebug() << "Created new list with id: " << list->id;
    ctx.result.data = list->id > 0;

    qs = QString("update RecommendationLists set minimum = :minimum, pick_ratio = :pick_ratio, "
                 " always_pick_at = :always_pick_at,  created = :created,"
                 "  quadratic_deviation = :quadratic_deviation, ratio_median = :ratio_median, "
                 "  distance_to_double_sigma = :distance_to_double_sigma,has_aux_data = :has_aux_data,"
                 "  use_weighting = :use_weighting, use_mood_adjustment = :use_mood_adjustment,"
                 " sources = :sources where name = :name");
    ctx.ReplaceQuery(qs);
    ctx.bindValue("minimum",list->minimumMatch);
    ctx.bindValue("pick_ratio",list->maxUnmatchedPerMatch);
    ctx.bindValue("always_pick_at",list->alwaysPickAt);
    ctx.bindValue("created",creationTimestamp);
    ctx.bindValue("quadratic_deviation",list->quadraticDeviation);
    ctx.bindValue("ratio_median",list->ratioMedian);
    ctx.bindValue("distance_to_double_sigma",list->sigma2Distance);
    ctx.bindValue("has_aux_data",list->hasAuxDataFilled);
    ctx.bindValue("use_weighting",list->useWeighting);
    ctx.bindValue("use_mood_adjustment",list->useMoodAdjustment);
    QStringList authors;
    for(auto id : list->ficData.authorIds)
        authors.push_back(QString::number(id));
    ctx.bindValue("sources",authors.join(","));
    ctx.bindValue("name",list->name);
    if(!ctx.ExecAndCheck())
    {
        list->id = -1;
        return ctx.result;
    }


    return ctx.result;
}

DiagnosticSQLResult<bool> UpdateFicCountForRecommendationList(int listId, QSqlDatabase db)
{
    QString qs = QString("update RecommendationLists set fic_count=(select count(fic_id) "
                         " from RecommendationListData where list_id = :list_id) where id = :list_id");
    SqlContext<bool> ctx(db, qs,{{"list_id",listId}});
    if(listId == -1 || !ctx.ExecAndCheck())
        return ctx.result;
    return ctx.result;
}

DiagnosticSQLResult<bool> DeleteTagFromDatabase(QString tag, QSqlDatabase db)
{
    SqlContext<bool> ctx(db);
    ctx.bindValue("tag", tag);
    ctx.ExecuteList({"delete from FicTags where tag = :tag",
                     "delete from tags where tag = :tag"});
    return ctx.result;
}

DiagnosticSQLResult<bool>  CreateTagInDatabase(QString tag, QSqlDatabase db)
{
    QString qs = QString("INSERT INTO TAGS(TAG) VALUES(:tag)");
    return SqlContext<bool>(db, qs,{{"tag",tag}})();
}

DiagnosticSQLResult<int>  GetRecommendationListIdForName(QString name, QSqlDatabase db)
{
    QString qs = QString("select id from RecommendationLists where name = :name");
    SqlContext<int> ctx(db, qs, {{"name", name}});
    ctx.FetchSingleValue<int>("id", 0);
    return ctx.result;
}

DiagnosticSQLResult<bool>  AddAuthorFavouritesToList(int authorId, int listId, QSqlDatabase db)
{
    QString qs = QString(" update RecommendationListData set match_count = match_count+1 where "
                         " list_id = :list_id "
                         " and fic_id in (select fic_id from recommendations r where recommender_id = :author_id)");
    return  SqlContext<bool>(db, qs,{{"author_id",authorId},{"list_id",listId}})();
}

DiagnosticSQLResult<bool>  ShortenFFNurlsForAllFics(QSqlDatabase db)
{
    QString qs = QString("update fanfics set url = cfReturnCapture('(/s/\\d+/)', url)");
    return  SqlContext<bool>(db, qs)();
}

DiagnosticSQLResult<bool> IsGenreList(QStringList list, QString website, QSqlDatabase db)
{
    QString qs = QString("select count(*) as idcount from genres where genre in(%1) and website = :website");
    qs = qs.arg("'" + list.join("','") + "'");

    SqlContext<int> ctx(db, qs, {{"website", website}});
    ctx.FetchSingleValue<int>("idcount", 0);

    DiagnosticSQLResult<bool> realResult;
    realResult.success = ctx.result.success;
    realResult.oracleError = ctx.result.oracleError;
    realResult.data = ctx.result.data > 0;
    return realResult;

}

DiagnosticSQLResult<QVector<int>> GetWebIdList(QString where, QString website, QSqlDatabase db)
{
    QVector<int> result;
    QString fieldName = website + "_id";
    QString qs = QString("select %1_id from fanfics %2").arg(website).arg(where);
    SqlContext<QVector<int>> ctx(db, qs);
    ctx.FetchLargeSelectIntoList<int>(fieldName, qs);
    return ctx.result;
}

DiagnosticSQLResult<QVector<int>> GetIdList(QString where, QSqlDatabase db)
{
    QString qs = QString("select id from fanfics %1 order by id asc").arg(where);
    SqlContext<QVector<int>> ctx(db, qs);
    ctx.FetchLargeSelectIntoList<int>("id", qs);
    return ctx.result;
}

DiagnosticSQLResult<bool> DeactivateStory(int id, QString website, QSqlDatabase db)
{
    QString qs = QString("update fanfics set alive = 0 where %1_id = :id");
    qs=qs.arg(website);
    return SqlContext<bool>(db, qs, {{"id",id}})();
}

DiagnosticSQLResult<bool> CreateAuthorRecord(core::AuthorPtr author, QDateTime timestamp, QSqlDatabase db)
{

    QString qs = " insert into recommenders(name, url, favourites, fics, page_updated, ffn_id, ao3_id,sb_id, sv_id, "
                 "page_creation_date, info_updated, last_favourites_update,last_favourites_checked) "
                 "values(:name, :url, :favourites, :fics,  :time, :ffn_id, :ao3_id,:sb_id, :sv_id, "
                 ":page_creation_date, :info_updated,:last_favourites_update,:last_favourites_checked) ";
    SqlContext<bool> ctx(db, qs);
    if(author->name.isEmpty())
        author->name = QUuid::createUuid().toString();
    ctx.bindValue("name", author->name);
    ctx.bindValue("url", author->url("ffn"));
    ctx.bindValue("favourites", author->favCount);
    ctx.bindValue("fics", author->ficCount);
    ctx.bindValue("time", timestamp);
    ctx.bindValue("ffn_id", author->GetWebID("ffn"));
    ctx.bindValue("ao3_id", author->GetWebID("ao3"));
    ctx.bindValue("sb_id", author->GetWebID("sb"));
    ctx.bindValue("sv_id", author->GetWebID("sv"));
    ctx.bindValue("page_creation_date", author->stats.pageCreated);
    ctx.bindValue("info_updated", author->stats.bioLastUpdated);
    ctx.bindValue("last_favourites_update", author->stats.favouritesLastUpdated);
    ctx.bindValue("last_favourites_checked", author->stats.favouritesLastChecked);
    ctx.ExecAndCheck();
    return ctx.result;
}

DiagnosticSQLResult<bool>  UpdateAuthorRecord(core::AuthorPtr author, QDateTime timestamp, QSqlDatabase db)
{

    QString qs = " update recommenders set name = :name, url = :url, favourites = :favourites, fics = :fics, page_updated = :page_updated, "
                 "page_creation_date= :page_creation_date, info_updated= :info_updated, "
                 " ffn_id = :ffn_id, ao3_id = :ao3_id, sb_id  =:sb_id, sv_id = :sv_id,"
                 " last_favourites_update = :last_favourites_update, "
                 " last_favourites_checked = :last_favourites_checked "
                 " where id = :id ";
    SqlContext<bool> ctx(db, qs);
    ctx.bindValue("name", author->name);
    ctx.bindValue("url", author->url("ffn"));
    ctx.bindValue("favourites", author->stats.favouriteStats.favourites);
    ctx.bindValue("fics", author->ficCount);
    ctx.bindValue("page_updated", timestamp);
    ctx.bindValue("page_creation_date", author->stats.pageCreated);
    ctx.bindValue("info_updated", author->stats.bioLastUpdated);
    ctx.bindValue("ffn_id", author->GetWebID("ffn"));
    ctx.bindValue("ao3_id", author->GetWebID("ao3"));
    ctx.bindValue("sb_id", author->GetWebID("sb"));
    ctx.bindValue("sv_id", author->GetWebID("sv"));
    ctx.bindValue("last_favourites_update", author->stats.favouritesLastUpdated);
    ctx.bindValue("last_favourites_checked", author->stats.favouritesLastChecked);
    ctx.bindValue("id", author->id);
    ctx.ExecAndCheck();

    //not reading those yet
    //q1.bindValue(":info_wordcount", author->ficCount);
    //    q1.bindValue(":last_published_fic_date", author->ficCount);
    //    q1.bindValue(":first_published_fic_date", author->ficCount);
    //    q1.bindValue(":own_wordcount", author->ficCount);
    //    q1.bindValue(":own_favourites", author->ficCount);
    //    q1.bindValue(":own_finished_ratio", author->ficCount);
    //    q1.bindValue(":most_written_size", author->ficCount);

    return ctx.result;
}

DiagnosticSQLResult<bool> UpdateAuthorFavouritesUpdateDate(int authorId, QDateTime date, QSqlDatabase db)
{
    QString qs = " update recommenders set"
                 " last_favourites_update = :last_favourites_update, "
                 " last_favourites_checked = :last_favourites_checked "
                 "where id = :id ";
    SqlContext<bool> ctx(db, qs);
    ctx.bindValue("id", authorId);
    ctx.bindValue("last_favourites_update", date);
    ctx.bindValue("last_favourites_checked", QDateTime::currentDateTime());
    ctx.ExecAndCheck();

    return ctx.result;
}


DiagnosticSQLResult<QStringList> ReadUserTags(QSqlDatabase db)
{
    QString qs = QString("Select tag from tags");
    SqlContext<QStringList> ctx(db);
    ctx.FetchLargeSelectIntoList<QString>("tag", qs);
    return ctx.result;
}

DiagnosticSQLResult<bool>  PushTaglistIntoDatabase(QStringList tagList, QSqlDatabase db)
{
    int counter = 0;
    QString qs = QString("INSERT INTO TAGS (TAG, id) VALUES (:tag, :id)");
    SqlContext<bool> ctx(db, qs);
    ctx.ExecuteWithKeyListAndBindFunctor<QString>(tagList, [&](QString tag, QSqlQuery q){
        q.bindValue(":tag", tag);
        q.bindValue(":id", counter);
        counter++;
    }, true);
    return ctx.result;
}

DiagnosticSQLResult<bool>  AssignNewNameForAuthor(core::AuthorPtr author, QString name, QSqlDatabase db)
{
    QString qs = " UPDATE recommenders SET name = :name where id = :id";
    SqlContext<bool> ctx(db, qs, {{"name", name},{"id", author->id}});
    if(author->GetIdStatus() != core::AuthorIdStatus::valid)
        return ctx.result;
    return ctx();
}

DiagnosticSQLResult<QList<int>> GetAllAuthorIds(QSqlDatabase db)
{
    QString qs = QString("select distinct id from recommenders");

    SqlContext<QList<int>> ctx(db);
    ctx.FetchLargeSelectIntoList<int>("id", qs);
    return ctx.result;
}

// not working, no idea why
//DiagnosticSQLResult<QSet<int> > GetAllMatchesWithRecsUID(QSharedPointer<core::RecommendationList> params, QString uid, QSqlDatabase db)
//{
//    SqlContext<QSet<int>> ctx(db);
//    ctx.bindValue("uid", uid);
//    ctx.bindValue("ratio", params->pickRatio);
//    ctx.bindValue("always_pick_at", params->alwaysPickAt);
//    ctx.bindValue("min", params->minimumMatch);
//    QLOG_INFO() << "always: " << params->alwaysPickAt;
//    QLOG_INFO() << "ratio: " << params->pickRatio;
//    QLOG_INFO() << "min: " << params->minimumMatch;
//    ctx.FetchLargeSelectIntoList<int>("id",
//                                      "select distinct id from recommenders rs where  ( "
//                                      " (select count(recommender_id) from recommendations where cfInRecommendations(fic_id, :uid) = 1 and recommender_id = rs.id) >= :min "
//                                      " and ( "
//                                      " (cast((select count(recommender_id) from recommendations where recommender_id = rs.id)  as float) "
//                                      " / "
//                                      " (cast((select count(recommender_id) from recommendations where cfInRecommendations(fic_id, :uid) = 1 and recommender_id = rs.id) as float))) <= :ratio)"
//                                      ") "
//                                      );
//    return ctx.result;
//}

DiagnosticSQLResult<QSet<int> > GetAllMatchesWithRecsUID(QSharedPointer<core::RecommendationList> params, QString uid, QSqlDatabase db)
{
    QLOG_INFO() << "always: " << params->alwaysPickAt;
    QLOG_INFO() << "ratio: " << params->maxUnmatchedPerMatch;
    QLOG_INFO() << "min: " << params->minimumMatch;
    QLOG_INFO() << "///////////FIRST FETCH////////";
    SqlContext<QSet<int>> ctx(db);
    ctx.bindValue("uid1", uid);
    ctx.bindValue("uid2", uid);
    ctx.bindValue("ratio", params->maxUnmatchedPerMatch);
    ctx.bindValue("min", params->minimumMatch);
    ctx.bindValue("always_pick_at", params->alwaysPickAt);
    //qDebug() << "feature avail: " << db.driver()->hasFeature(QSqlDriver::NamedPlaceholders);
    ctx.FetchLargeSelectIntoList<int>("id",
                                      "select id,  "
                                      " (select count(recommender_id) from recommendations rs where "
                                      " cfInRecommendations(rs.fic_id, :uid1) = 1 and rs.recommender_id = id ) as matches, "

                                      " ("
                                      " (cast((select count(recommender_id) from recommendations rs1 where rs1.recommender_id = id)  as float) "
                                      " / "
                                      " (cast((select count(recommender_id) from recommendations rs2 where"
                                      " cfInRecommendations(rs2.fic_id, :uid2) = 1"
                                      " and rs2.recommender_id = id) as float)))"
                                      " )  as ratio"

                                      " from recommenders  "
                                      " where (ratio <= :ratio and  matches >= :min ) or matches >= :always_pick_at "
                                      );

    return ctx.result;
}


DiagnosticSQLResult<QSet<int> > ConvertFFNSourceFicsToDB(QString uid, QSqlDatabase db)
{
    SqlContext<QSet<int>> ctx(db);
    ctx.bindValue("uid", uid);
    ctx.FetchLargeSelectIntoList<int>("id",
                                      "select id from fanfics where cfInSourceFics(ffn_id)");

    return ctx.result;
}

static auto getFicWeightPtrFromQuery = [](auto& q){
    core::FicWeightPtr fw(new core::FicForWeightCalc);
    fw->adult = q.value("Rated").toString() == "M";
    fw->authorId = q.value("author_id").toInt();
    fw->complete = q.value("complete").toBool();
    //fw->chapterCount = q.value("chapters").toInt();
    fw->dead = !fw->complete  && q.value("updated").toDateTime().daysTo(QDateTime::currentDateTime()) > 365;
    fw->fandoms.push_back(q.value("fandom1").toInt());
    fw->fandoms.push_back(q.value("fandom2").toInt());
    fw->favCount = q.value("favourites").toInt();
    fw->published = q.value("published").toDate();
    fw->updated = q.value("updated").toDate();
    fw->genreString = q.value("genres").toString();
    fw->id = q.value("id").toInt();
    fw->reviewCount = q.value("reviews").toInt();
    fw->slash = q.value("filter_pass_1").toBool();
    fw->wordCount = q.value("wordcount").toInt();
    return fw;
};

DiagnosticSQLResult<QHash<uint32_t, core::FicWeightPtr>> GetFicsForRecCreation(QSqlDatabase db)
{
    SqlContext<QHash<uint32_t, core::FicWeightPtr>> ctx(db);
    QString qs = QString("select id,rated, chapters, author_id, complete, updated, "
                         "fandom1,fandom2,favourites, published, updated,"
                         "  genres, reviews, filter_pass_1, wordcount"
                         "  from fanfics where cfInSourceFics(ffn_id) order by id asc");
    ctx.FetchSelectFunctor(qs, DATAQ{
                               auto fw = getFicWeightPtrFromQuery(q);
                               if(fw)
                                data[static_cast<uint32_t>(fw->id)] = fw;
                           });
    return ctx.result;


}
DiagnosticSQLResult<bool> ConvertFFNTaggedFicsToDB(QHash<int, int>& hash, QSqlDatabase db)
{
    SqlContext<int> ctx(db);
    QLOG_INFO() << "keys list:" << hash.keys();
    for(int ffn_id: hash.keys())
    {
        ctx.bindValue("ffn_id", ffn_id);
        ctx.FetchSingleValue<int>("id", -1,false, "select id from fanfics where ffn_id = :ffn_id");
        QString error = ctx.result.oracleError;
        if(!error.isEmpty() && error != "no data to read")
        {
            QLOG_ERROR() << "///ERROR///" << error;
            ctx.result.success = false;
            break;
        }
        if(ctx.result.data != -1)
            hash[ffn_id] = ctx.result.data;
        else
            hash.remove(ffn_id);
    }

    QLOG_INFO() << "Resulting list:" << hash;

    SqlContext<bool> ctx1(db);
    ctx1.result.success = ctx.result.success;
    ctx1.result.oracleError = ctx.result.oracleError;
    return ctx1.result;
}

DiagnosticSQLResult<bool> ConvertDBFicsToFFN(QHash<int, int>& hash, QSqlDatabase db)
{
    SqlContext<int> ctx(db);

    for(int id: hash.keys())
    {
        ctx.bindValue("id", id);
        ctx.FetchSingleValue<int>("ffn_id", -1, true, "select ffn_id from fanfics where id = :id");
        QString error = ctx.result.oracleError;
        if(!error.isEmpty() && error != "no data to read")
        {
            ctx.result.success = false;
            break;
        }
        hash[id] = ctx.result.data;
    }

    SqlContext<bool> ctx1(db);
    ctx1.result.success = ctx.result.success;
    ctx1.result.oracleError = ctx.result.oracleError;
    return ctx1.result;
}



DiagnosticSQLResult<bool> ResetActionQueue(QSqlDatabase db)
{
    QString qs = QString("update fanfics set queued_for_action = 0");
    SqlContext<bool> ctx(db, qs);
    ctx();
    return ctx.result;
}

DiagnosticSQLResult<bool> WriteDetectedGenres(QVector<genre_stats::FicGenreData> fics, QSqlDatabase db)
{

    QString qsClenaup = QString("update fanfics set "
                                "true_genre1 = '', "
                                "true_genre1_percent = 0,"
                                "true_genre2 = '', "
                                "true_genre2_percent = 0,"
                                "true_genre3 = '',"
                                "true_genre3_percent = 0,"
                                "max_genre_percent = 0, "
                                "kept_genres = ''" );
    SqlContext<bool> cleanup(db, qsClenaup);
    cleanup();

    QString qs = QString("update fanfics set "
                         " true_genre1 = :true_genre1, "
                         " true_genre1_percent = :true_genre1_percent,"
                         " true_genre2 = :true_genre2, "
                         " true_genre2_percent = :true_genre2_percent,"
                         " true_genre3 = :true_genre3, "
                         " true_genre3_percent = :true_genre3_percent,"
                         " max_genre_percent = :max_genre_percent,"
                         " kept_genres =:kept_genres where id = :fic_id" );
    SqlContext<bool> ctx(db, qs);
    for(auto fic : fics)
    {
        for(int i = 0; i < 3; i++)
        {
            genre_stats::GenreBit genre;
            if(i < fic.processedGenres.size())
                genre = fic.processedGenres.at(i);
            else
                genre.relevance = 0;

            QString writtenGenre = genre.genres.join(",");
            if(writtenGenre.isEmpty())
                genre.relevance = 0;

            ctx.bindValue("true_genre" + QString::number(i+1), writtenGenre);
            ctx.bindValue("true_genre" + QString::number(i+1) + "_percent", genre.relevance > 1 ? 1 : genre.relevance );


        }
        ctx.bindValue("max_genre_percent", fic.maxGenrePercent);
//        if(fic.maxGenrePercent > 0)
//            qDebug() << "Writing max_genre_percent: " << fic.maxGenrePercent;
        ctx.bindValue("kept_genres", fic.keptToken);
        ctx.bindValue("fic_id", fic.ficId);
        if(!ctx.ExecAndCheck())
            return ctx.result;
    }
    return ctx.result;
}


DiagnosticSQLResult<bool> WriteDetectedGenresIteration2(QVector<genre_stats::FicGenreData> fics, QSqlDatabase db)
{
    QString qsClenaup = QString("delete from FIC_GENRE_ITERATIONS" );
    SqlContext<bool> cleanup(db, qsClenaup);
    cleanup();
    QString qs = QString(" insert into fic_genre_iterations("
                         "fic_id, "
                         " true_genre1, "
                         " true_genre1_percent,"
                         " true_genre2, "
                         " true_genre2_percent,"
                         " true_genre3,"
                         " true_genre3_percent,"
                         " kept_genres,"
                         " max_genre_percent"
                         ")"
                         " values("
                         " :fic_id, "
                         " :true_genre1, "
                         " :true_genre1_percent,"
                         " :true_genre2, "
                         " :true_genre2_percent,"
                         " :true_genre3,"
                         " :true_genre3_percent,"
                         " :kept_genres,"
                         " :max_genre_percent"
                         ")");
    SqlContext<bool> ctx(db, qs);
    for(auto fic : fics)
    {
        for(int i = 0; i < 3; i++)
        {
            genre_stats::GenreBit genre;
            if(i < fic.processedGenres.size())
                genre = fic.processedGenres.at(i);
            else
                genre.relevance = 0;

            QString writtenGenre = genre.genres.join(",");
            if(writtenGenre.isEmpty())
                genre.relevance = 0;

            ctx.bindValue("true_genre" + QString::number(i+1), writtenGenre);
            ctx.bindValue("true_genre" + QString::number(i+1) + "_percent", genre.relevance > 1 ? 1 : genre.relevance );


        }
        ctx.bindValue("max_genre_percent", fic.maxGenrePercent);
        ctx.bindValue("kept_genres", fic.keptToken);
        ctx.bindValue("fic_id", fic.ficId);
        if(!ctx.ExecAndCheck())
            return ctx.result;
    }
    return ctx.result;
}

DiagnosticSQLResult<QHash<int, QList<genre_stats::GenreBit>>> GetFullGenreList(QSqlDatabase db,bool useOriginalOnly)
{
    QString qs = QString("select id, genres, "
                         " true_genre1, "
                         " true_genre1_percent,"
                         " true_genre2, "
                         " true_genre2_percent,"
                         " true_genre3,"
                         " true_genre3_percent"
                         " from fanfics");

    SqlContext<QHash<int, QList<genre_stats::GenreBit>>> ctx (db, qs);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        QList<genre_stats::GenreBit> dataForFic;
        auto id = q.value("id").toInt();
        QString genres = q.value("genres").toString();
        if(q.value("true_genre1").toString().trimmed().isEmpty() || useOriginalOnly)
        {
            // genres not detected

            genres = genres.replace("Hurt/Comfort", "HurtComfort");
            auto list = genres.split("/");
            list.replaceInStrings("HurtComfort","Hurt/Comfort");
            for(auto genreBit: list)
            {
                genre_stats::GenreBit bit;
                bit.genres.push_back(genreBit);
                bit.isInTheOriginal = true;
                bit.relevance = 1;
                dataForFic.push_back(bit);
            }
        }
        else{

            // genres detected
            for(int i = 1; i < 4; i++)
            {
                QString tgKey = "true_genre" + QString::number(i);
                QString tgKeyValue = "true_genre" + QString::number(i) + "_percent";
                QString genre = q.value(tgKey).toString();
                if(genre.isEmpty()){
                    break;
                }

                genre_stats::GenreBit bit;
                bit.genres = genre.split(QRegExp("[\\s,]"), QString::SkipEmptyParts);
                bit.relevance = q.value(tgKeyValue).toFloat();
                bit.isDetected = true;
                for(auto genreBit : bit.genres)
                    if(genres.contains(genreBit))
                        bit.isInTheOriginal = true;

                dataForFic.push_back(bit);
            }
        }
        ctx.result.data[id] = dataForFic;
    });
    return ctx.result;
}


DiagnosticSQLResult<QHash<int, int> > GetMatchesForUID(QString uid, QSqlDatabase db)
{
    QString qs = QString("select fic_id, count(fic_id) as cnt from recommendations where cfInAuthors(recommender_id, :uid) = 1 group by fic_id");
    SqlContext<QHash<int, int> > ctx(db);
    ctx.bindValue("uid", uid);
    ctx.FetchSelectFunctor(qs, [](QHash<int, int>& data, QSqlQuery& q){
        int fic = q.value("fic_id").toInt();
        int matches = q.value("cnt").toInt();
        //QLOG_INFO() << " fic_id: " << fic << " matches: " << matches;
        data[fic] = matches;
    });
    return ctx.result;
}

DiagnosticSQLResult<QStringList> GetAllAuthorFavourites(int id, QSqlDatabase db)
{
    QString qs = QString("select id, ffn_id, ao3_id, sb_id, sv_id from fanfics where id in (select fic_id from recommendations where recommender_id = :author_id )");
    SqlContext<QStringList> ctx (db, qs, {{"author_id", id}});
    ctx.ForEachInSelect([&](QSqlQuery& q){
        auto ffn_id = q.value("ffn_id").toInt();
        auto ao3_id = q.value("ao3_id").toInt();
        auto sb_id = q.value("sb_id").toInt();
        auto sv_id = q.value("sv_id").toInt();

        if(ffn_id != -1)
            ctx.result.data.push_back(url_utils::GetStoryUrlFromWebId(ffn_id, "ffn"));
        if(ao3_id != -1)
            ctx.result.data.push_back(url_utils::GetStoryUrlFromWebId(ao3_id, "ao3"));
        if(sb_id != -1)
            ctx.result.data.push_back(url_utils::GetStoryUrlFromWebId(sb_id, "sb"));
        if(sv_id != -1)
            ctx.result.data.push_back(url_utils::GetStoryUrlFromWebId(sv_id, "sv"));
    });
    return ctx.result;

}

DiagnosticSQLResult<QList<int>> GetAllAuthorRecommendationIDs(int id, QSqlDatabase db)
{
    QString qs = QString("select distinct fic_id from recommendations where recommender_id = :author_id");
    SqlContext<QList<int>> ctx (db, qs, {{"author_id", id}});
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data.push_back(q.value("fic_id").toInt());
    });
    return ctx.result;
}

DiagnosticSQLResult<bool> IncrementAllValuesInListMatchingAuthorFavourites(int authorId, int listId, QSqlDatabase db)
{
    QString qs = QString(" update RecommendationListData set match_count = match_count+1 where "
                         " list_id = :list_id "
                         " and fic_id in (select fic_id from recommendations r where recommender_id = :author_id)");
    return SqlContext<bool> (db, qs, {{"author_id", authorId},{"list_id", listId}})();
}


DiagnosticSQLResult<bool> DecrementAllValuesInListMatchingAuthorFavourites(int authorId, int listId, QSqlDatabase db)
{
    QString qs = QString(" update RecommendationListData set match_count = match_count-1 where "
                         " list_id = :list_id "
                         " and fic_id in (select fic_id from recommendations r where recommender_id = :author_id)");

    SqlContext<bool> ctx (db, qs, {{"author_id", authorId},{"list_id", listId}});
    if(!ctx.ExecAndCheck())
        return ctx.result;

    ctx.ReplaceQuery("delete from RecommendationListData where match_count <= 0");
    ctx.ExecAndCheck();
    return  ctx.result;
}

DiagnosticSQLResult<QSet<QString>> GetAllGenres(QSqlDatabase db)
{
    QString qs = QString("select genre from genres");
    SqlContext<QSet<QString>> ctx(db);
    ctx.FetchLargeSelectIntoList<QString>("genre", qs);
    return ctx.result;
}

static core::FandomPtr FandomfromQueryNew (QSqlQuery& q, core::FandomPtr fandom = core::FandomPtr())
{
    if(!fandom)
    {
        fandom = core::Fandom::NewFandom();
        fandom->id = q.value("ID").toInt();
        fandom->SetName(q.value("name").toString());
        fandom->tracked = q.value("tracked").toInt();
        fandom->lastUpdateDate = q.value("updated").toDate();
        fandom->AddUrl({q.value("url").toString(),
                        q.value("website").toString(),
                        ""});
    }
    else{
        fandom->AddUrl({q.value("url").toString(),
                        q.value("website").toString(),
                        ""});
    }
    return fandom;

}
static DiagnosticSQLResult<bool> GetFandomStats(core::FandomPtr fandom, QSqlDatabase db)
{
    QString qs = QString("select * from fandomsources where global_id = :id");
    SqlContext<bool> ctx(db, qs);
    if(!fandom)
        return ctx.result;
    ctx.bindValue("id",fandom->id);
    ctx.ExecAndCheck();
    if(ctx.Next())
    {
        fandom->source = ctx.value("website").toString();
        fandom->ficCount = ctx.value("fic_count").toInt();
        fandom->averageFavesTop3 = ctx.value("average_faves_top_3").toDouble();
        fandom->dateOfCreation = ctx.value("date_of_creation").toDate();
        fandom->dateOfFirstFic = ctx.value("date_of_first_fic").toDate();
        fandom->dateOfLastFic = ctx.value("date_of_last_fic").toDate();
    }
    return ctx.result;
}

DiagnosticSQLResult<QList<core::FandomPtr>> GetAllFandoms(QSqlDatabase db)
{
    int fandomsSize = 0;
    {
        QString qs = QString(" select count(*) as cn from fandomindex");
        SqlContext<int> ctx(db, qs);
        ctx.FetchSingleValue<int>("cn", 1000);
        fandomsSize = ctx.result.data;
    }

    core::FandomPtr currentFandom;
    int lastId = -1;
    QString qs = QString(" select ind.id as id, ind.name as name, ind.tracked as tracked, urls.url as url, urls.website as website,"
                         " urls.custom as section, ind.updated as updated "
                         " from fandomindex ind left join fandomurls urls"
                         " on ind.id = urls.global_id order by id asc");
    SqlContext<QList<core::FandomPtr>> ctx(db, qs);
    ctx.result.data.reserve(fandomsSize);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        auto currentId= q.value("id").toInt();
        if(currentId != lastId)
        {
            currentFandom = FandomfromQueryNew(q);
            //GetFandomStats(currentFandom, db);
            ctx.result.data.push_back(currentFandom);
        }
        else
            currentFandom = FandomfromQueryNew(q, currentFandom);
        lastId = currentId;
    });
    return ctx.result;
}
DiagnosticSQLResult<QList<core::FandomPtr> > GetAllFandomsAfter(int id, QSqlDatabase db)
{
    int fandomsSize = 0;
    {
        QString qs = QString(" select count(*) as cn from fandomindex where id > :id");
        SqlContext<int> ctx(db, qs, BP1(id));
        ctx.FetchSingleValue<int>("cn", 1000);
        fandomsSize = ctx.result.data;
    }

    core::FandomPtr currentFandom;
    int lastId = -1;
    QString qs = QString(" select ind.id as id, ind.name as name, ind.tracked as tracked, urls.url as url, urls.website as website,"
                         " urls.custom as section, ind.updated as updated "
                         " from fandomindex ind left join fandomurls urls"
                         " on ind.id = urls.global_id where ind.id > :id order by id asc ");
    SqlContext<QList<core::FandomPtr>> ctx(db, qs, BP1(id));
    ctx.result.data.reserve(fandomsSize);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        auto currentId= q.value("id").toInt();
        if(currentId != lastId)
        {
            currentFandom = FandomfromQueryNew(q);
            //GetFandomStats(currentFandom, db);
            ctx.result.data.push_back(currentFandom);
        }
        else
            currentFandom = FandomfromQueryNew(q, currentFandom);
        lastId = currentId;
    });
    return ctx.result;
}
DiagnosticSQLResult<core::FandomPtr> GetFandom(QString fandom, bool loadFandomStats, QSqlDatabase db)
{
    core::FandomPtr currentFandom;

    QString qs = QString(" select ind.id as id, ind.name as name, ind.tracked as tracked, urls.url as url, urls.website as website,"
                         " urls.custom as section, ind.updated as updated from fandomindex ind left join fandomurls urls on ind.id = urls.global_id"
                         " where name = :fandom ");
    SqlContext<core::FandomPtr> ctx(db, qs, BP1(fandom));

    ctx.ForEachInSelect([&](QSqlQuery& q){
        currentFandom = FandomfromQueryNew(q, currentFandom);
    });
    if(loadFandomStats)
    {
        auto statResult = GetFandomStats(currentFandom, db);
        if(!statResult.success)
        {
            ctx.result.success = false;
            return ctx.result;
        }
    }

    ctx.result.data = currentFandom;
    return ctx.result;
}

DiagnosticSQLResult<core::FandomPtr> GetFandom(int id, bool loadFandomStats, QSqlDatabase db)
{
    core::FandomPtr currentFandom;

    QString qs = QString(" select ind.id as id, ind.name as name, ind.tracked as tracked, urls.url as url, urls.website as website,"
                         " urls.custom as section, ind.updated as updated from fandomindex ind left join fandomurls urls on ind.id = urls.global_id"
                         " where id = :id");
    SqlContext<core::FandomPtr> ctx(db, qs, BP1(id));

    ctx.ForEachInSelect([&](QSqlQuery& q){
        currentFandom = FandomfromQueryNew(q, currentFandom);
    });
    if(loadFandomStats)
    {
        auto statResult = GetFandomStats(currentFandom, db);
        if(!statResult.success)
        {
            ctx.result.success = false;
            return ctx.result;
        }
    }

    ctx.result.data = currentFandom;
    return ctx.result;
}

DiagnosticSQLResult<bool> IgnoreFandom(int fandom_id, bool including_crossovers, QSqlDatabase db)
{
    QString qs = QString(" insert into ignored_fandoms (fandom_id, including_crossovers) values (:fandom_id, :including_crossovers) ");
    SqlContext<bool> ctx(db, qs,  BP2(fandom_id,including_crossovers));
    return ctx(true);
}

DiagnosticSQLResult<bool> SetUserProfile(int id,  QSqlDatabase db)
{
    QString qs = QString(" update user_settings set value = :id where name = 'user_ffn_id' ");
    SqlContext<bool> ctx(db, qs,  BP1(id));
    return ctx(true);
}

DiagnosticSQLResult<int> GetUserProfile(QSqlDatabase db)
{
    QString qs = QString("select value from user_settings where name = 'user_ffn_id'");
    SqlContext<int> ctx(db, qs);
    ctx.FetchSingleValue<int>("value", -1);
    return ctx.result;
}
DiagnosticSQLResult<int> GetRecommenderIDByFFNId(int id, QSqlDatabase db)
{
    QString qs = QString("select id from recommenders where ffn_id = :id");
    SqlContext<int> ctx(db, qs, BP1(id));
    ctx.FetchSingleValue<int>("id", -1);
    return ctx.result;
}

DiagnosticSQLResult<bool> RemoveFandomFromIgnoredList(int fandom_id, QSqlDatabase db)
{
    QString qs = QString(" delete from ignored_fandoms where fandom_id  = :fandom_id");
    return SqlContext<bool>(db, qs, BP1(fandom_id))();
}

DiagnosticSQLResult<QStringList> GetIgnoredFandoms(QSqlDatabase db)
{
    QString qs = QString("select name from fandomindex where id in (select fandom_id from ignored_fandoms) order by name asc");
    SqlContext<QStringList> ctx(db);
    ctx.FetchLargeSelectIntoList<QString>("name", qs);
    return ctx.result;
}

DiagnosticSQLResult<QHash<int, QString>> GetFandomNamesForIDs(QList<int>ids, QSqlDatabase db)
{
    SqlContext<QHash<int, QString> > ctx(db);
    QString qs = QString("select id, name from fandomindex where id in (%1)");
    QStringList inParts;
    for(auto id: ids)
        inParts.push_back("'" + QString::number(id) + "'");
    if(inParts.size() == 0)
        return ctx.result;
    qs = qs.arg(inParts.join(","));

    ctx.FetchSelectFunctor(qs, [](QHash<int, QString>& data, QSqlQuery& q){
        data[q.value("id").toInt()] = q.value("name").toString();
    });
    return ctx.result;
}

DiagnosticSQLResult<QHash<int, bool> > GetIgnoredFandomIDs(QSqlDatabase db)
{
    QString qs = QString("select fandom_id, including_crossovers from ignored_fandoms order by fandom_id asc");
    SqlContext<QHash<int, bool> > ctx(db);
    ctx.FetchSelectFunctor(qs, [](QHash<int, bool>& data, QSqlQuery& q){
        data[q.value("fandom_id").toInt()] = q.value("including_crossovers").toBool();
    }, true);
    return ctx.result;
}


DiagnosticSQLResult<bool> IgnoreFandomSlashFilter(int fandom_id, QSqlDatabase db)
{
    QString qs = QString(" insert into ignored_fandoms_slash_filter (fandom_id) values (:fandom_id) ");
    SqlContext<bool> ctx(db, qs, BP1(fandom_id));
    return ctx(true);
}

DiagnosticSQLResult<bool> RemoveFandomFromIgnoredListSlashFilter(int fandom_id, QSqlDatabase db)
{
    QString qs = QString(" delete from ignored_fandoms_slash_filter where fandom_id  = :fandom_id");
    return SqlContext<bool>(db, qs, BP1(fandom_id))();
}

DiagnosticSQLResult<QStringList> GetIgnoredFandomsSlashFilter(QSqlDatabase db)
{
    QString qs = QString("select name from fandomindex where id in (select fandom_id from ignored_fandoms_slash_filter) order by name asc");
    SqlContext<QStringList> ctx(db);
    ctx.FetchLargeSelectIntoList<QString>("name", qs);
    return ctx.result;
}

DiagnosticSQLResult<bool> CleanupFandom(int fandom_id, QSqlDatabase db)
{
    SqlContext<bool> ctx(db);
    ctx.bindValue("fandom_id", fandom_id);
    ctx.ExecuteList({"delete from fanfics where id in (select distinct fic_id from ficfandoms where fandom_id = :fandom_id)",
                     "delete from fictags where fic_id in (select distinct fic_id from ficfandoms where fandom_id = :fandom_id)",
                     "delete from recommendationlistdata where fic_id in (select distinct fic_id from ficfandoms where fandom_id = :fandom_id)",
                     "delete from ficfandoms where fandom_id = :fandom_id"});
    return ctx.result;
}

DiagnosticSQLResult<bool> DeleteFandom(int fandom_id, QSqlDatabase db)
{
    SqlContext<bool> ctx(db);
    ctx.bindValue("fandom_id", fandom_id);
    ctx.ExecuteList({"delete from fandomindex where id = :fandom_id",
                     "delete from fandomurls where global_id = :fandom_id"});
    return ctx.result;
}

DiagnosticSQLResult<QStringList> GetTrackedFandomList(QSqlDatabase db)
{
    QString qs = QString(" select name from fandomindex where tracked = 1 order by name asc");
    SqlContext<QStringList> ctx(db);
    ctx.FetchLargeSelectIntoList<QString>("name", qs);
    return ctx.result;
}

DiagnosticSQLResult<int> GetFandomCountInDatabase(QSqlDatabase db)
{
    QString qs = QString("Select count(name) as cn from fandomindex");
    SqlContext<int> ctx(db, qs);
    ctx.FetchSingleValue<int>("cn", 0);
    return ctx.result;
}


DiagnosticSQLResult<bool> AddFandomForFic(int fic_id, int fandom_id, QSqlDatabase db)
{
    QString qs = QString(" insert into ficfandoms (fic_id, fandom_id) values (:fic_id, :fandom_id) ");
    SqlContext<bool> ctx(db, qs, BP2(fic_id,fandom_id));

    if(fic_id == -1 || fandom_id == -1)
        return ctx.result;

    return ctx(true);
}

DiagnosticSQLResult<QStringList>  GetFandomNamesForFicId(int fic_id, QSqlDatabase db)
{
    QString qs = QString("select name from fandomindex where fandomindex.id in (select fandom_id from ficfandoms ff where ff.fic_id = :fic_id)");
    SqlContext<QStringList> ctx(db, qs, BP1(fic_id));
    ctx.ForEachInSelect([&](QSqlQuery& q){
        auto fandom = q.value("name").toString().trimmed();
        if(!fandom.contains("????"))
            ctx.result.data.push_back(fandom);
    });
    return ctx.result;
}

DiagnosticSQLResult<bool> AddUrlToFandom(int fandomID, core::Url url, QSqlDatabase db)
{
    QString qs = QString(" insert into fandomurls (global_id, url, website, custom) "
                         " values (:global_id, :url, :website, :custom) ");

    SqlContext<bool> ctx(db, qs,{{"global_id",fandomID},
                                 {"url",url.GetUrl()},
                                 {"website",url.GetSource()},
                                 {"custom",url.GetType()}});
    if(fandomID == -1)
        return ctx.result;
    return ctx(true);
}

DiagnosticSQLResult<QList<int>> GetRecommendersForFicIdAndListId(int fic_id, QSqlDatabase db)
{
    QString qs = QString("Select distinct recommender_id from recommendations where fic_id = :fic_id");
    SqlContext<QList<int>> ctx(db, qs, BP1(fic_id));
    ctx.FetchLargeSelectIntoList<int>("recommender_id", qs);
    return ctx.result;
}

DiagnosticSQLResult<QSet<int> > GetAllTaggedFics(bool allowSnoozed, QSqlDatabase db)
{
        QString qs = QString("select distinct fic_id from fictags ");
        QStringList parts;

        if(!allowSnoozed)
            parts.push_back(" fic_id not in (select distinct fic_id from ficsnoozes where expired = 1) ");
        if(allowSnoozed)
            parts.push_back(" tag <> 'Snoozed'");

        if(parts.size() > 0)
        {
            qs+= " where ";
            qs+= parts.join(" and ");
        }
        qDebug() << "snooze query: "  << qs;
        SqlContext<QSet<int>> ctx(db, qs);
        ctx.FetchLargeSelectIntoList<int>("fic_id", qs);
        return ctx.result;
}

DiagnosticSQLResult<QSet<int>> GetFicsTaggedWith(QStringList tags, bool useAND, bool allowSnoozed, QSqlDatabase db){

    if(!useAND)
    {
        QString qs = QString("select distinct fic_id from fictags ");
        QStringList parts;

        if(tags.size() > 0)
            parts.push_back(QString("tag in ('%1')").arg(tags.join("','")));

        if(parts.size() > 0)
        {
            qs+= " where ";
            qs+= parts.join(" and ");
        }
        if(!allowSnoozed)
            qs+= " and fic_id not in (select distinct fic_id from ficsnoozes where expired = 0) ";
        qDebug() << "snooze tags query: "  << qs;
        SqlContext<QSet<int>> ctx(db, qs);
        ctx.FetchLargeSelectIntoList<int>("fic_id", qs);
        return ctx.result;
    }
    else {
        QString qs = QString("select distinct fic_id from fictags ft where ");
        QString prototype = " exists (select fic_id from fictags where ft.fic_id = fic_id and tag = '%1') ";
        QStringList parts;

        QStringList tokens;
        for(auto tag : tags)
        {
            tokens.push_back(prototype.arg(tag));
        }
        qs += tokens.join(" and ");
        if(!allowSnoozed)
            qs+= " and fic_id not in (select distinct fic_id from ficsnoozes where expired = 0) ";

        //qs += " and " + parts.join(" and ");
        SqlContext<QSet<int>> ctx(db, qs);
        ctx.FetchLargeSelectIntoList<int>("fic_id", qs);
        return ctx.result;
    }
}

DiagnosticSQLResult<QSet<int> > GetAuthorsForTags(QStringList tags, QSqlDatabase db){
    QString qs = QString("select distinct author_id from ficauthors ");
    qs += QString(" where fic_id in (select distinct fic_id from fictags where tag in ('%1'))").arg(tags.join("','"));
    SqlContext<QSet<int>> ctx(db, qs);
    ctx.FetchLargeSelectIntoList<int>("author_id", qs);
    return ctx.result;
}

DiagnosticSQLResult<QHash<int, core::SnoozeInfo> > GetSnoozeInfo(QSqlDatabase db)
{
    QString qs = "select id, complete, chapters from fanfics where cfInFicSelection(id) > 0";
    SqlContext<QHash<int, core::SnoozeInfo>> ctx(db, qs);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        qDebug() << " loading snooze data";
        core::SnoozeInfo info;
        info.ficId = q.value("id").toInt();
        info.finished = q.value("complete").toInt();
        info.atChapter = q.value("chapters").toInt();
        qDebug() << " ficid: " << info.ficId ;
        qDebug() << " finished: " << info.finished;
        qDebug() << " atChapter: " << info.atChapter;
        ctx.result.data[info.ficId] = info;
    });
    return ctx.result;
}

DiagnosticSQLResult<QHash<int, core::SnoozeTaskInfo>> GetUserSnoozeInfo(bool limitedSelection, QSqlDatabase db){
    QString qs = "select fic_id, snooze_added, snoozed_until_finished, snoozed_at_chapter,  snoozed_till_chapter, expired from ficsnoozes %1 order by fic_id asc";

    if(limitedSelection)
        qs = qs.arg(" where cfInFicSelection(fic_id) > 0 ");
    else
        qs = qs.arg("");

    SqlContext<QHash<int, core::SnoozeTaskInfo>> ctx(db, qs);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        core::SnoozeTaskInfo info;
        info.ficId =                q.value("fic_id").toInt();
        info.added =                q.value("snooze_added").toDateTime();
        info.expired =              q.value("expired").toInt();
        info.untilFinished =        q.value("snoozed_until_finished").toInt();
        info.snoozedAtChapter=      q.value("snoozed_at_chapter").toInt();
        info.snoozedTillChapter =   q.value("snoozed_till_chapter").toInt();

        ctx.result.data[info.ficId] = info;
    });
    return ctx.result;
}


DiagnosticSQLResult<QHash<int, QString>> GetNotesForFics(bool limitedSelection , QSqlDatabase db){
    QString qs = "select * from ficnotes %1 order by fic_id asc";

    if(limitedSelection)
        qs = qs.arg(" where cfInFicSelection(fic_id) > 0 ");
    else
        qs = qs.arg("");

    SqlContext<QHash<int, QString>> ctx(db, qs);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data[q.value("fic_id").toInt()] = q.value("note_content").toString();
    });
    return ctx.result;
}

DiagnosticSQLResult<QHash<int, int>> GetReadingChaptersForFics(bool limitedSelection, QSqlDatabase db)
{
    QString qs = "select * from FicReadingTracker %1 order by fic_id asc";

    if(limitedSelection)
        qs = qs.arg(" where cfInFicSelection(fic_id) > 0 ");
    else
        qs = qs.arg("");

    SqlContext<QHash<int, int>> ctx(db, qs);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data[q.value("fic_id").toInt()] = q.value("at_chapter").toInt();
    });
    return ctx.result;
}

DiagnosticSQLResult<bool> WriteExpiredSnoozes(QSet<int> data,QSqlDatabase db){
    QString qs = QString("update ficsnoozes set expired = 1 where fic_id = :fic_id");
    SqlContext<bool> ctx(db, qs);
    for(auto ficId : data)
    {
        ctx.bindValue("fic_id", ficId);
        if(!ctx.ExecAndCheck())
            break;
    }
    return ctx.result;
}

DiagnosticSQLResult<bool> SnoozeFic(core::SnoozeTaskInfo data,QSqlDatabase db){
    QString qs = "INSERT INTO FicSnoozes(fic_id, snoozed_at_chapter, snoozed_till_chapter, snoozed_until_finished, snooze_added)"
                 " values(:fic_id, :snoozed_at_chapter, :snoozed_till_chapter, :snoozed_until_finished,  date('now')) "
                 " on conflict (fic_id) "
                 " do update set "
                 " snoozed_at_chapter = :snoozed_at_chapter_, "
                 " snoozed_till_chapter = :snoozed_till_chapter_, "
                 " snoozed_until_finished = :snoozed_until_finished_,"
                 " snooze_added = date('now'), "
                 " expired = 0 "
                 " where fic_id = :fic_id_";
    SqlContext<bool> ctx(db, qs);
    ctx.bindValue("fic_id", data.ficId);
    ctx.bindValue("snoozed_at_chapter", data.snoozedAtChapter);
    ctx.bindValue("snoozed_till_chapter", data.snoozedTillChapter);
    ctx.bindValue("snoozed_until_finished", data.untilFinished);
    ctx.bindValue("snoozed_at_chapter_", data.snoozedAtChapter);
    ctx.bindValue("snoozed_till_chapter_", data.snoozedTillChapter);
    ctx.bindValue("snoozed_until_finished_", data.untilFinished);
    ctx.bindValue("fic_id_", data.ficId);
    ctx.bindValue("fic_id", data.ficId);
    ctx.ExecAndCheck();
    return ctx.result;
}

DiagnosticSQLResult<bool> RemoveSnooze(int fic_id,QSqlDatabase db){
    QString qs = QString("delete from FicSnoozes where fic_id = :fic_id");
    return SqlContext<bool> (db, qs, BP1(fic_id))();
}

DiagnosticSQLResult<bool> AddNoteToFic(int fic_id, QString note, QSqlDatabase db)
{
    QString qs = "INSERT INTO ficnotes(fic_id, note_content) values(:fic_id, :note) "
                 "on conflict (fic_id) do update set note_content = :note_ where fic_id = :fic_id_";
    SqlContext<bool> ctx(db, qs);
    ctx.bindValue("fic_id", fic_id);
    ctx.bindValue("note", note);
    ctx.bindValue("note_", note);
    ctx.bindValue("fic_id_", fic_id);
    ctx.ExecAndCheck();
    return ctx.result;
}

DiagnosticSQLResult<bool> RemoveNoteFromFic(int fic_id, QSqlDatabase db)
{
    QString qs = QString("delete from ficnotes where fic_id = :fic_id");
    return SqlContext<bool> (db, qs, BP1(fic_id))();
}


DiagnosticSQLResult<QVector<int> > GetAllFicsThatDontHaveDBID(QSqlDatabase db)
{
    QString qs = QString("select distinct ffn_id from fictags where fic_id < 1");
    SqlContext<QVector<int>> ctx(db, qs);
    ctx.FetchLargeSelectIntoList<int>("ffn_id", qs);
    return ctx.result;
}

DiagnosticSQLResult<bool> FillDBIDsForFics(QVector<core::IdPack> pack, QSqlDatabase db)
{
    QString qs = QString("update fictags set fic_id = :id where ffn_id = :ffn_id");
    SqlContext<bool> ctx(db, qs);
    for(const core::IdPack& id: pack)
    {
        if(id.db < 1)
            continue;

        ctx.bindValue("id", id.db);
        ctx.bindValue("ffn_id", id.ffn);
        if(!ctx.ExecAndCheck())
        {
            ctx.result.success = false;
            break;
        }
    }
    return ctx.result;
}

DiagnosticSQLResult<bool> FetchTagsForFics(QVector<core::Fic> * fics, QSqlDatabase db)
{
    QString qs = QString("select fic_id,  group_concat(tag, ' ')  as tags from fictags where cfInSourceFics(fic_id) > 0 group by fic_id");
    QHash<int, QString> tags;
    auto* data= ThreadData::GetRecommendationData();
    auto& hash = data->sourceFics;

    for(const auto& fic : *fics)
        hash.insert(fic.id);

    SqlContext<bool> ctx(db, qs);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        tags[q.value("fic_id").toInt()] = q.value("tags").toString();
    });
    for(auto& fic : *fics)
        fic.tags = tags[fic.id];
    return ctx.result;
}


template <typename T1, typename T2>
inline double DivideAsDoubles(T1 arg1, T2 arg2){
    return static_cast<double>(arg1)/static_cast<double>(arg2);
}
DiagnosticSQLResult<bool> FetchRecommendationsBreakdown(QVector<core::Fic> * fics, int listId, QSqlDatabase db)
{
    QString qs = QString("select fic_id,  "
                         "breakdown_available,"
                         "votes_common, votes_uncommon, votes_rare, votes_unique, "
                         "value_common, value_uncommon, value_rare, value_unique, purged "
                         "from RecommendationListData where "
                         " cfInSourceFics(fic_id) > 0 and list_id = :listId"
                         " group by fic_id");
    QHash<int, QStringList> breakdown;
    QHash<int, QStringList> breakdownCounts;
    auto* data= ThreadData::GetRecommendationData();
    auto& sourceSet = data->sourceFics;

    for(const auto& fic : *fics)
        sourceSet.insert(fic.id);
    QSet<int> purgedFics;
    SqlContext<bool> ctx(db, qs);
    ctx.bindValue("listId", listId);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        auto ficId = q.value("fic_id").toInt();
        int sumtotal = q.value("value_common").toInt() +
                q.value("value_uncommon").toInt() +
                q.value("value_rare").toInt() +
                q.value("value_unique").toInt();
        int common = static_cast<int>(DivideAsDoubles(q.value("value_common").toInt(),sumtotal)*100);
        int uncommon = static_cast<int>(DivideAsDoubles(q.value("value_uncommon").toInt(),sumtotal)*100);
        int rare = static_cast<int>(DivideAsDoubles(q.value("value_rare").toInt(),sumtotal)*100);
        int unique = static_cast<int>(DivideAsDoubles(q.value("value_unique").toInt(),sumtotal)*100);
        bool purged = q.value("purged").toBool();
        if(purged == true)
            purgedFics.insert(ficId);

        breakdown[ficId].push_back(QString::number(common));
        breakdown[ficId].push_back(QString::number(uncommon));
        breakdown[ficId].push_back(QString::number(rare));
        breakdown[ficId].push_back(QString::number(unique));


        breakdownCounts[ficId].push_back(q.value("votes_common").toString());
        breakdownCounts[ficId].push_back(q.value("votes_uncommon").toString());
        breakdownCounts[ficId].push_back(q.value("votes_rare").toString());
        breakdownCounts[ficId].push_back(q.value("votes_unique").toString());


    });


    for(auto& fic : *fics)
    {
        fic.voteBreakdown = breakdown[fic.id];
        fic.voteBreakdownCounts = breakdownCounts[fic.id];
        if(purgedFics.contains(fic.id))
            fic.purged = true;
    }
    return ctx.result;
}

DiagnosticSQLResult<QSharedPointer<core::RecommendationList>> FetchParamsForRecList(int id, QSqlDatabase db)
{
    QString qs = QString(" select * from recommendationlists where id = :id ");
    SqlContext<QSharedPointer<core::RecommendationList>> ctx(db, qs, BP1(id));
    //QSharedPointer<core::RecommendationList> params(new core::RecommendationList);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data = QSharedPointer<core::RecommendationList>{new core::RecommendationList};
        ctx.result.data->id = q.value("id").toInt();
        ctx.result.data->name = q.value("name").toString();
        ctx.result.data->maxUnmatchedPerMatch = q.value("pick_ratio").toDouble();
        ctx.result.data->alwaysPickAt = q.value("always_pick_at").toInt();
        ctx.result.data->minimumMatch = q.value("minimum").toInt();
        ctx.result.data->useWeighting = q.value("use_weighting").toInt();
        ctx.result.data->useMoodAdjustment= q.value("use_mood_adjustment").toInt();
    });
    return ctx.result;
}

DiagnosticSQLResult<bool> SetFicsAsListOrigin(QVector<int> ficIds, int list_id, QSqlDatabase db)
{
    QString qs = QString("update RecommendationListData set is_origin = 1 where fic_id = :fic_id and list_id = :list_id");
    SqlContext<bool> ctx(db, qs, BP1(list_id));
    ctx.ExecuteWithValueList<int>("fic_id", ficIds);
    return ctx.result;
}

DiagnosticSQLResult<bool> DeleteLinkedAuthorsForAuthor(int author_id,  QSqlDatabase db)
{
    QString qs = QString("delete from LinkedAuthors where recommender_id = :author_id");
    return SqlContext<bool> (db, qs, BP1(author_id))();
}

DiagnosticSQLResult<bool>  UploadLinkedAuthorsForAuthor(int author_id, QString website, QList<int> ids, QSqlDatabase db)
{
    QString qs = QString("insert into  LinkedAuthors(recommender_id, %1_id) values(:author_id, :id)").arg(website);
    SqlContext<bool> ctx(db, qs, BP1(author_id));
    ctx.ExecuteWithValueList<int>("id", ids, true);
    return ctx.result;
}

DiagnosticSQLResult<QVector<int> > GetAllUnprocessedLinkedAuthors(QSqlDatabase db)
{
    QString qs = QString("select distinct ffn_id from linkedauthors where ffn_id not in (select ffn_id from recommenders)");
    SqlContext<QVector<int>> ctx(db, qs);
    ctx.FetchLargeSelectIntoList<int>("ffn_id", qs);
    return ctx.result;
}



DiagnosticSQLResult<QStringList> GetLinkedPagesForList(int list_id, QString website, QSqlDatabase db)
{
    QString qs = QString("Select distinct %1_id from LinkedAuthors "
                         " where recommender_id in ( select author_id from RecommendationListAuthorStats where list_id = %2) "
                         " and %1_id not in (select distinct %1_id from recommenders)"
                         " union all "
                         " select ffn_id from recommenders where id not in (select distinct recommender_id from recommendations) and favourites != 0 ");
    qs=qs.arg(website).arg(list_id);

    SqlContext<QStringList> ctx(db, qs, BP1(list_id));
    ctx.ForEachInSelect([&](QSqlQuery& q){
        auto authorUrl = url_utils::GetAuthorUrlFromWebId(q.value(QString("%1_id").arg(website)).toInt(), "ffn");
        ctx.result.data.push_back(authorUrl);
    });
    return ctx.result;
}

DiagnosticSQLResult<bool> RemoveAuthorRecommendationStatsFromDatabase(int list_id, int author_id, QSqlDatabase db)
{
    QString qs = QString("delete from recommendationlistauthorstats "
                         " where list_id = :list_id and author_id = :author_id");
    return SqlContext<bool>(db, qs, BP2(list_id,author_id))();
}

DiagnosticSQLResult<bool> CreateFandomIndexRecord(int id, QString name, QSqlDatabase db)
{
    QString qs = QString("insert into fandomindex(id, name) values(:id, :name)");
    return SqlContext<bool>(db, qs, BP2(name, id))();
}



DiagnosticSQLResult<QHash<int, QList<int>>> GetWholeFicFandomsTable(QSqlDatabase db)
{
    QString qs = QString("select fic_id, fandom_id from ficfandoms");
    SqlContext<QHash<int, QList<int>>> ctx(db, qs);
    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data[q.value("fandom_id").toInt()].push_back(q.value("fic_id").toInt());
    });
    return ctx.result;
}

DiagnosticSQLResult<bool> EraseFicFandomsTable(QSqlDatabase db)
{
    QString qs = QString("delete from ficfandoms");
    return SqlContext<bool>(db, qs)();
}

DiagnosticSQLResult<bool> SetLastUpdateDateForFandom(int id, QDate updated, QSqlDatabase db)
{
    QString qs = QString("update fandomindex set updated = :updated where id = :id");
    return SqlContext<bool>(db, qs, BP2(updated, id))();
}

DiagnosticSQLResult<bool> RemoveFandomFromRecentList(QString name, QSqlDatabase db)
{
    QString qs = QString("delete from recent_fandoms where fandom = :name");
    return SqlContext<bool>(db, qs, BP1(name))();
}


DiagnosticSQLResult<int> GetLastExecutedTaskID(QSqlDatabase db)
{
    QString qs = QString("select max(id) as maxid from pagetasks");
    SqlContext<int>ctx(db, qs);
    ctx.FetchSingleValue<int>("maxid", -1);
    return ctx.result;
}

// new query limit
DiagnosticSQLResult<bool> GetTaskSuccessByID(int id, QSqlDatabase db)
{
    QString qs = QString("select success from pagetasks where id = :id");
    SqlContext<bool>ctx(db, qs, BP1(id));
    ctx.FetchSingleValue<bool>("success", false);
    return ctx.result;
}
DiagnosticSQLResult<bool>  IsForceStopActivated(int id, QSqlDatabase db)
{
    QString qs = QString("select force_stop from pagetasks where id = :id");
    SqlContext<bool>ctx(db, qs, BP1(id));
    ctx.FetchSingleValue<bool>("force_stop", false);
    return ctx.result;
}


void FillPageTaskBaseFromQuery(BaseTaskPtr task, QSqlQuery& q){
    if(!NullPtrGuard(task))
        return;
    task->created = q.value("created_at").toDateTime();
    task->scheduledTo = q.value("scheduled_to").toDateTime();
    task->startedAt = q.value("started_at").toDateTime();
    task->finishedAt= q.value("finished_at").toDateTime();
    task->type = q.value("type").toInt();
    task->size = q.value("size").toInt();
    task->retries= q.value("retries").toInt();
    task->success = q.value("success").toBool();
    task->finished= q.value("finished").toBool();
    task->addedFics = q.value("inserted_fics").toInt();
    task->addedAuthors= q.value("inserted_authors").toInt();
    task->updatedFics= q.value("updated_fics").toInt();
    task->updatedAuthors= q.value("updated_authors").toInt();
    task->parsedPages = q.value("parsed_pages").toInt();
}


void FillPageTaskFromQuery(PageTaskPtr task, QSqlQuery& q){
    if(!NullPtrGuard(task))
        return;

    FillPageTaskBaseFromQuery(task, q);
    task->id= q.value("id").toInt();
    task->parts = q.value("parts").toInt();
    task->results = q.value("results").toString();
    task->taskComment= q.value("task_comment").toString();
    task->size = q.value("task_size").toInt();
    task->allowedRetries = q.value("allowed_retry_count").toInt();
    task->cacheMode = static_cast<ECacheMode>(q.value("cache_mode").toInt());
    task->refreshIfNeeded= q.value("refresh_if_needed").toBool();
}

void FillSubTaskFromQuery(SubTaskPtr task, QSqlQuery& q){
    if(!NullPtrGuard(task))
        return;

    FillPageTaskBaseFromQuery(task, q);
    task->parentId= q.value("task_id").toInt();
    task->id = q.value("sub_id").toInt();
    SubTaskContentBasePtr content;

    if(task->type == 0)
    {
        content = SubTaskAuthorContent::NewContent();
        auto cast = static_cast<SubTaskAuthorContent*>(content.data());
        cast->authors = q.value("content").toString().split("\n");
    }
    if(task->type == 1)
    {
        content = SubTaskFandomContent::NewContent();
        auto cast = static_cast<SubTaskFandomContent*>(content.data());
        cast->urlLinks = q.value("content").toString().split("\n");
        cast->fandom = q.value("custom_data1").toString();
        cast->fandom = q.value("custom_data1").toString();
    }
    task->updateLimit = q.value("parse_up_to").toDateTime();
    task->content = content;
    task->isValid = true;
    task->isNew =false;
}

DiagnosticSQLResult<PageTaskPtr> GetTaskData(int id, QSqlDatabase db)
{
    QString qs = QString("select * from pagetasks where id = :id");
    SqlContext<PageTaskPtr>ctx(db, qs, BP1(id));
    ctx.result.data = PageTask::CreateNewTask();
    if(!ctx.ExecAndCheckForData())
        return ctx.result;

    FillPageTaskFromQuery(ctx.result.data, ctx.q);
    return ctx.result;
}

DiagnosticSQLResult<SubTaskList> GetSubTaskData(int id, QSqlDatabase db)
{
    QString qs = QString("select * from PageTaskParts where task_id = :id");

    SqlContext<SubTaskList>ctx(db, qs, {{"id", id}});
    return ctx.ForEachInSelect([&](QSqlQuery& q){
        auto subtask = PageSubTask::CreateNewSubTask();
        FillSubTaskFromQuery(subtask, q);
        ctx.result.data.push_back(subtask); });
}

void FillPageFailuresFromQuery(PageFailurePtr failure, QSqlQuery& q){
    if(!NullPtrGuard(failure))
        return;

    failure->action = QSharedPointer<PageTaskAction>(new PageTaskAction{q.value("action_uuid").toString(),
                                                                        q.value("task_id").toInt(),
                                                                        q.value("sub_id").toInt()});
    failure->attemptTimeStamp = q.value("process_attempt").toDateTime();
    failure->errorCode  = static_cast<PageFailure::EFailureReason>(q.value("error_code").toInt());
    failure->errorlevel  = static_cast<PageFailure::EErrorLevel>(q.value("error_level").toInt());
    failure->lastSeen = q.value("last_seen_at").toDateTime();
    failure->url = q.value("url").toString();
    failure->error = q.value("error").toString();
}

void FillActionFromQuery(PageTaskActionPtr action, QSqlQuery& q){
    if(!NullPtrGuard(action))
        return;

    action->SetData(q.value("action_uuid").toString(),
                    q.value("task_id").toInt(),
                    q.value("sub_id").toInt());

    action->success = q.value("success").toBool();
    action->started = q.value("started_at").toDateTime();
    action->finished = q.value("finished_at").toDateTime();
    action->isNewAction = false;
}

DiagnosticSQLResult<SubTaskErrors> GetErrorsForSubTask(int id,  QSqlDatabase db, int subId)
{
    QString qs = QString("select * from PageWarnings where task_id = :id");
    bool singleSubTask = subId != -1;
    if(singleSubTask)
        qs+= " and sub_id = :sub_id";

    SqlContext<SubTaskErrors>ctx(db, qs, BP1(id));

    if(singleSubTask)
        ctx.bindValue("sub_id", subId);

    ctx.ForEachInSelect([&](QSqlQuery& q){
        auto failure = PageFailure::CreateNewPageFailure();
        FillPageFailuresFromQuery(failure, q);
        ctx.result.data.push_back(failure);
    });
    return ctx.result;
}

DiagnosticSQLResult<PageTaskActions> GetActionsForSubTask(int id, QSqlDatabase db, int subId)
{

    QString qs = QString("select * from PageTaskActions where task_id = :id");
    bool singleSubTask = subId != -1;
    if(singleSubTask)
        qs+= " and sub_id = :sub_id";
    SqlContext<PageTaskActions>ctx(db, qs, BP1(id));

    if(singleSubTask)
        ctx.bindValue("sub_id", subId);

    ctx.ForEachInSelect([&](QSqlQuery& q){
        auto action = PageTaskAction::CreateNewAction();
        FillActionFromQuery(action, q);
        ctx.result.data.push_back(action);
    });

    return ctx.result;
}

DiagnosticSQLResult<int> CreateTaskInDB(PageTaskPtr task, QSqlDatabase db)
{
    QString qs = QString("insert into PageTasks(type, parts, created_at, scheduled_to,  allowed_retry_count, "
                         "allowed_subtask_retry_count, cache_mode, refresh_if_needed, task_comment, task_size, success, finished,"
                         "parsed_pages, updated_fics,inserted_fics,inserted_authors,updated_authors) "
                         "values(:type, :parts, :created_at, :scheduled_to, :allowed_retry_count,"
                         ":allowed_subtask_retry_count, :cache_mode, :refresh_if_needed, :task_comment,:task_size, 0, 0,"
                         " :parsed_pages, :updated_fics,:inserted_fics,:inserted_authors,:updated_authors) ");

    SqlContext<int>ctx(db, qs);
    ctx.result.data = -1;
    ctx.bindValue("type", task->type);
    ctx.bindValue("parts", task->parts);
    ctx.bindValue("created_at", task->created);
    ctx.bindValue("scheduled_to", task->scheduledTo);
    ctx.bindValue("allowed_retry_count", task->allowedRetries);
    ctx.bindValue("allowed_subtask_retry_count", task->allowedSubtaskRetries);
    ctx.bindValue("cache_mode", static_cast<int>(task->cacheMode));
    ctx.bindValue("refresh_if_needed", task->refreshIfNeeded);
    ctx.bindValue("task_comment", task->taskComment);
    ctx.bindValue("task_size", task->size);
    ctx.bindValue("parsed_pages",      task->parsedPages);
    ctx.bindValue("updated_fics",      task->updatedFics);
    ctx.bindValue("inserted_fics",     task->addedFics);
    ctx.bindValue("inserted_authors",  task->addedAuthors);
    ctx.bindValue("updated_authors",   task->updatedAuthors);
    if(!ctx.ExecAndCheck())
        return ctx.result;

    ctx.ReplaceQuery("select max(id) as maxid from PageTasks");
    ctx.FetchSingleValue<int>("maxid", -1);
    return ctx.result;

}

DiagnosticSQLResult<bool> CreateSubTaskInDB(SubTaskPtr subtask, QSqlDatabase db)
{
    QString qs = QString("insert into PageTaskParts(task_id, type, sub_id, created_at, scheduled_to, content,task_size, success, finished, parse_up_to,"
                         "custom_data1, parsed_pages, updated_fics,inserted_fics,inserted_authors,updated_authors) "
                         "values(:task_id, :type, :sub_id, :created_at, :scheduled_to, :content,:task_size, 0,0, :parse_up_to,"
                         ":custom_data1, :parsed_pages, :updated_fics,:inserted_fics,:inserted_authors,:updated_authors) ");
    SqlContext<bool>ctx(db, qs);

    ctx.bindValue("task_id", subtask->parentId);
    ctx.bindValue("type", subtask->type);
    ctx.bindValue("sub_id", subtask->id);
    ctx.bindValue("created_at", subtask->created);
    ctx.bindValue("scheduled_to", subtask->scheduledTo);
    ctx.bindValue("content", subtask->content->ToDB());
    ctx.bindValue("task_size", subtask->size);
    ctx.bindValue("parse_up_to", subtask->updateLimit);

    QString customData = subtask->content->CustomData1();
    ctx.bindValue("custom_data1",      customData);
    ctx.bindValue("parsed_pages",      subtask->parsedPages);
    ctx.bindValue("updated_fics",      subtask->updatedFics);
    ctx.bindValue("inserted_fics",     subtask->addedFics);
    ctx.bindValue("inserted_authors",  subtask->addedAuthors);
    ctx.bindValue("updated_authors",   subtask->updatedAuthors);
    return ctx();

}

DiagnosticSQLResult<bool> CreateActionInDB(PageTaskActionPtr action, QSqlDatabase db)
{
    QString qs = QString("insert into PageTaskActions(action_uuid, task_id, sub_id, started_at, finished_at, success) "
                         "values(:action_uuid, :task_id, :sub_id, :started_at, :finished_at, :success) ");
    SqlContext<bool>ctx(db, qs);
    ctx.bindValue("action_uuid", action->id.toString());
    ctx.bindValue("task_id", action->taskId);
    ctx.bindValue("sub_id", action->subTaskId);
    ctx.bindValue("started_at", action->started);
    ctx.bindValue("finished_at", action->finished);
    ctx.bindValue("success", action->success);

    return ctx();
}

DiagnosticSQLResult<bool> CreateErrorsInDB(SubTaskErrors errors, QSqlDatabase db)
{
    QString qs = QString("insert into PageWarnings(action_uuid, task_id, sub_id, url, attempted_at, last_seen_at, error_code, error_level, error) "
                         "values(:action_uuid, :task_id, :sub_id, :url, :attempted_at, :last_seen, :error_code, :error_level, :error) ");

    SqlContext<bool>ctx(db, qs);
    ctx.ExecuteWithKeyListAndBindFunctor<PageFailurePtr>(errors, [](PageFailurePtr error, QSqlQuery& q){
        q.bindValue(":action_uuid", error->action->id.toString());
        q.bindValue(":task_id", error->action->taskId);
        q.bindValue(":sub_id", error->action->subTaskId);
        q.bindValue(":url", error->url);
        q.bindValue(":attempted_at", error->attemptTimeStamp);
        q.bindValue(":last_seen", error->lastSeen);
        q.bindValue(":error_code", static_cast<int>(error->errorCode));
        q.bindValue(":error_level", static_cast<int>(error->errorlevel));
        q.bindValue(":error", error->error);
    });
    return ctx.result;
}

DiagnosticSQLResult<bool> UpdateTaskInDB(PageTaskPtr task, QSqlDatabase db)
{

    QString qs = QString("update PageTasks set scheduled_to = :scheduled_to, started_at = :started, finished_at = :finished_at,"
                         " results = :results, retries = :retries, success = :success, task_size = :size, finished = :finished,"
                         " parsed_pages = :parsed_pages, updated_fics = :updated_fics, inserted_fics = :inserted_fics,"
                         " inserted_authors = :inserted_authors, updated_authors = :updated_authors"
                         " where id = :id");
    SqlContext<bool>ctx(db, qs);

    ctx.bindValue("scheduled_to",      task->scheduledTo);
    ctx.bindValue("started_at",        task->startedAt);
    ctx.bindValue("finished_at",       task->finishedAt);
    ctx.bindValue("finished",          task->finished);
    ctx.bindValue("results",           task->results);
    ctx.bindValue("retries",           task->retries);
    ctx.bindValue("success",           task->success);
    ctx.bindValue("id",                task->id);
    ctx.bindValue("size",              task->size);
    ctx.bindValue("parsed_pages",      task->parsedPages);
    ctx.bindValue("updated_fics",      task->updatedFics);
    ctx.bindValue("inserted_fics",     task->addedFics);
    ctx.bindValue("inserted_authors",  task->addedAuthors);
    ctx.bindValue("updated_authors",   task->updatedAuthors);

    return ctx();
}

DiagnosticSQLResult<bool> UpdateSubTaskInDB(SubTaskPtr task, QSqlDatabase db)
{
    QString qs = QString("update PageTaskParts set scheduled_to = :scheduled_to, started_at = :started_at, finished_at = :finished_at,"
                         " retries = :retries, success = :success, finished = :finished, "
                         " parsed_pages = :parsed_pages, updated_fics = :updated_fics, inserted_fics = :inserted_fics,"
                         " inserted_authors = :inserted_authors, updated_authors = :updated_authors, custom_data1 = :custom_data1"
                         " where task_id = :task_id and sub_id = :sub_id");

    SqlContext<bool>ctx(db, qs);
    QString customData = task->content->CustomData1();

    ctx.bindValue("scheduled_to",     task->scheduledTo);
    ctx.bindValue("started_at",       task->startedAt);
    ctx.bindValue("finished_at",      task->finishedAt);
    ctx.bindValue("retries",          task->retries);
    ctx.bindValue("success",          task->success);
    ctx.bindValue("finished",         task->finished);
    ctx.bindValue("parsed_pages",      task->parsedPages);
    ctx.bindValue("updated_fics",      task->updatedFics);
    ctx.bindValue("inserted_fics",     task->addedFics);
    ctx.bindValue("inserted_authors",  task->addedAuthors);
    ctx.bindValue("updated_authors",   task->updatedAuthors);
    ctx.bindValue("custom_data1",      customData);

    ctx.bindValue("task_id",          task->parentId);
    ctx.bindValue("sub_id",           task->id);

    return ctx();


}

DiagnosticSQLResult<bool> SetTaskFinished(int id, QSqlDatabase db)
{
    return SqlContext<bool> (db, "update PageTasks set finished = 1 where id = :task_id",{
                                 {":task_id", id}
                             })();
}

DiagnosticSQLResult<TaskList> GetUnfinishedTasks(QSqlDatabase db)
{
    SqlContext<QList<int>> ctx1(db);
    ctx1.FetchLargeSelectIntoList<int>("id", "select id from pagetasks where finished = 0");

    SqlContext<TaskList> ctx2(db, "select * from pagetasks where id = :task_id");
    if(!ctx1.result.success)
        return ctx2.result;

    for(auto id: ctx1.result.data)
    {
        ctx2.bindValue("task_id",id);
        if(!ctx2.ExecAndCheckForData())
            return ctx2.result;
        auto task = PageTask::CreateNewTask();
        FillPageTaskFromQuery(task, ctx2.q);
        ctx2.result.data.push_back(task);
    }
    return ctx2.result;
}



// !!!! requires careful testing!
DiagnosticSQLResult<bool> ExportTagsToDatabase(QSqlDatabase originDB, QSqlDatabase targetDB)
{
    thread_local auto idHash = GetGlobalIDHash(originDB, " where id in (select distinct fic_id from fictags)").data;
    {

        QStringList targetKeyList = {"ffn_id","ao3_id","sb_id","sv_id","tag",};
        QStringList sourceKeyList = {"ffn","ao3","sb","sv","tag",};
        QString insertQS = QString("insert into UserFicTags(ffn_id, ao3_id, sb_id, sv_id, tag) values(:ffn_id, :ao3_id, :sb_id, :sv_id, :tag)");
        ParallelSqlContext<bool> ctx (originDB, "select fic_id, tag from fictags order by fic_id, tag", sourceKeyList,
                                      targetDB, insertQS, targetKeyList);

        auto keyConverter = [&](QString sourceKey, QSqlQuery q, QSqlDatabase , auto& )->QVariant
        {
            auto record = idHash.GetRecord(q.value("fic_id").toInt());
            return record.GetID(sourceKey);
        };

        ctx.valueConverters["ffn"] = keyConverter;
        ctx.valueConverters["ao3"] = keyConverter;
        ctx.valueConverters["sb"] = keyConverter;
        ctx.valueConverters["sv"] = keyConverter;
        ctx();
        if(!ctx.Success())
            return ctx.result;
    }
    {
        QStringList keyList = {"tag","id"};
        QString insertQS = QString("insert into UserTags(fic_id, tag) values(:id, :tag)");
        ParallelSqlContext<bool> ctx (originDB, "select * from tags", keyList,
                                      targetDB, insertQS, keyList);
        return ctx();
    }
}

// !!!! requires careful testing!
DiagnosticSQLResult<bool> ImportTagsFromDatabase(QSqlDatabase currentDB,QSqlDatabase tagImportSourceDB)
{
    {
        SqlContext<bool> ctxTarget(currentDB, QStringList{"delete from fictags","delete from tags"});
        if(!ctxTarget.result.success)
            return ctxTarget.result;
    }

    {
        QStringList keyList = {"tag","id"};
        QString insertQS = QString("insert into Tags(id, tag) values(:id, :tag)");
        ParallelSqlContext<bool> ctx (tagImportSourceDB, "select * from UserTags", keyList,
                                      currentDB, insertQS, keyList);
        ctx();
        if(!ctx.Success())
            return ctx.result;
    }
    // need to ensure fic_id in the source database
    SqlContext<bool> ctxTarget(tagImportSourceDB, QStringList{"alter table UserFicTags add column fic_id integer default -1"});
    ctxTarget();
    if(!ctxTarget.result.success)
        return ctxTarget.result;

    //    SqlContext<bool> ctxTest(currentDB, QStringList{"insert into FicTags(fic_id, ffn_id, ao3_id, sb_id, sv_id, tag)"
    //                                                    " values(-1, -1, -1, -1, -1, -1)"});
    //    ctxTest();
    //    if(!ctxTest.result.success)
    //        return ctxTest.result;

    QStringList keyList = {"fic_id", "ffn_id", "ao3_id", "sb_id", "sv_id", "tag"};
    QString insertQS = QString("insert into FicTags(fic_id, ffn_id, ao3_id, sb_id, sv_id, tag) values(:fic_id, :ffn_id, :ao3_id, :sb_id, :sv_id, :tag)");
    //bool isOpen = currentDB.isOpen();
    ParallelSqlContext<bool> ctx (tagImportSourceDB, "select * from UserFicTags", keyList,
                                  currentDB, insertQS, keyList);
    return ctx();
}


DiagnosticSQLResult<bool> ExportSlashToDatabase(QSqlDatabase originDB, QSqlDatabase targetDB)
{

    QStringList keyList = {"ffn_id","keywords_result","keywords_yes","keywords_no","filter_pass_1", "filter_pass_2"};
    QString insertQS = QString("insert into slash_data_ffn(ffn_id, keywords_result, keywords_yes, keywords_no, filter_pass_1, filter_pass_2) "
                               " values(:ffn_id, :keywords_result, :keywords_yes, :keywords_no, :filter_pass_1, :filter_pass_2) ");

    return ParallelSqlContext<bool> (originDB, "select * from slash_data_ffn", keyList,
                                     targetDB, insertQS, keyList)();
}

DiagnosticSQLResult<bool> ImportSlashFromDatabase(QSqlDatabase slashImportSourceDB, QSqlDatabase appDB)
{

    {
        SqlContext<bool> ctxTarget(appDB, "delete from slash_data_ffn");
        if(ctxTarget.ExecAndCheck())
            return ctxTarget.result;
    }

    QStringList keyList = {"ffn_id","keywords_result","keywords_yes","keywords_no","filter_pass_1", "filter_pass_2"};
    QString insertQS = QString("insert into slash_data_ffn(ffn_id, keywords_yes, keywords_no, keywords_result, filter_pass_1, filter_pass_2)"
                               "  values(:ffn_id, :keywords_yes, :keywords_no, :keywords_result, :filter_pass_1, :filter_pass_2)");
    return ParallelSqlContext<bool> (slashImportSourceDB, "select * from slash_data_ffn", keyList,
                                     appDB, insertQS, keyList)();
}



FanficIdRecord::FanficIdRecord()
{
    ids["ffn"] = -1;
    ids["sb"] = -1;
    ids["sv"] = -1;
    ids["ao3"] = -1;
}

DiagnosticSQLResult<int> FanficIdRecord::CreateRecord(QSqlDatabase db) const
{
    QString query = "INSERT INTO FANFICS (ffn_id, sb_id, sv_id, ao3_id, for_fill, lastupdate) "
                    "VALUES ( :ffn_id, :sb_id, :sv_id, :ao3_id, 1, date('now'))";

    SqlContext<int> ctx(db, query,
    {{"ffn_id", ids["ffn"]},
     {"sb_id", ids["sb"]},
     {"sv_id", ids["sv"]},
     {"ao3_id", ids["ao3"]},});

    if(!ctx.ExecAndCheck())
        return ctx.result;

    ctx.ReplaceQuery("select max(id) as mid from fanfics");
    ctx.FetchSingleValue<int>("mid", 0);
    return ctx.result;
}

DiagnosticSQLResult<bool> AddFandomLink(int oldId, int newId, QSqlDatabase db)
{
    SqlContext<bool> ctx(db, "select * from fandoms where id = :old_id",
    {{"old_id", oldId}});
    if(!ctx.ExecAndCheckForData())
        return ctx.result;

    QStringList urls;
    urls << ctx.trimmedValue("normal_url");
    urls << ctx.trimmedValue("crossover_url");

    urls.removeAll("");
    urls.removeAll("none");

    QString custom = ctx.trimmedValue("section");

    ctx.ReplaceQuery("insert into fandomurls (global_id, url, website, custom) values(:new_id, :url, 'ffn', :custom)");
    ctx.bindValue("custom", custom);
    ctx.bindValue("new_id",newId);
    ctx.ExecuteWithKeyListAndBindFunctor<QString>(urls, [](QString url, QSqlQuery& q){
        q.bindValue(":url", url);
    });
    return ctx.result;
}

DiagnosticSQLResult<bool> WriteAuthorFavouriteStatistics(core::AuthorPtr author, QSqlDatabase db)
{
    QString query = "INSERT INTO AuthorFavouritesStatistics ("
                    "author_id, favourites, favourites_wordcount, average_words_per_chapter, esrb_type, prevalent_mood,"
                    "most_favourited_size,favourites_type,average_favourited_length,favourite_fandoms_diversity, explorer_factor, "
                    "mega_explorer_factor, crossover_factor,unfinished_factor,esrb_uniformity_factor,esrb_kiddy,esrb_mature,"
                    "genre_diversity_factor, mood_uniformity_factor, mood_sad, mood_neutral, mood_happy, "
                    "crack_factor,slash_factor,smut_factor, prevalent_genre, size_tiny, size_medium, size_large, size_huge,"
                    "first_published, last_published"
                    ") "
                    "VALUES ("
                    ":author_id, :favourites, :favourites_wordcount, :average_words_per_chapter, :esrb_type, :prevalent_mood,"
                    ":most_favourited_size,:favourites_type,:average_favourited_length,:favourite_fandoms_diversity, :explorer_factor, "
                    ":mega_explorer_factor, :crossover_factor,:unfinished_factor,:esrb_uniformity_factor,:esrb_kiddy,:esrb_mature,"
                    ":genre_diversity_factor, :mood_uniformity_factor, :mood_sad, :mood_neutral, :mood_happy, "
                    ":crack_factor,:slash_factor,:smut_factor, :prevalent_genre, :size_tiny, :size_medium, :size_large, :size_huge,"
                    ":first_published, :last_published"
                    ")";
    SqlContext<bool> ctx(db, query);

    auto& stats = author->stats.favouriteStats;
    ctx.bindValue("author_id", author->id);
    ctx.bindValue("favourites", stats.favourites);
    ctx.bindValue("favourites_wordcount", stats.ficWordCount);
    ctx.bindValue("average_words_per_chapter", stats.averageWordsPerChapter);
    ctx.bindValue("esrb_type", static_cast<int>(stats.esrbType));
    ctx.bindValue("prevalent_mood", static_cast<int>(stats.prevalentMood));
    ctx.bindValue("most_favourited_size", static_cast<int>(stats.mostFavouritedSize));
    ctx.bindValue("favourites_type", static_cast<int>(stats.sectionRelativeSize));
    ctx.bindValue("average_favourited_length", stats.averageLength);
    ctx.bindValue("favourite_fandoms_diversity", stats.fandomsDiversity);
    ctx.bindValue("explorer_factor", stats.explorerFactor);
    ctx.bindValue("mega_explorer_factor", stats.megaExplorerFactor);
    ctx.bindValue("crossover_factor", stats.crossoverFactor);
    ctx.bindValue("unfinished_factor", stats.unfinishedFactor);
    ctx.bindValue("esrb_uniformity_factor", stats.esrbUniformityFactor);
    ctx.bindValue("esrb_kiddy", stats.esrbKiddy);
    ctx.bindValue("esrb_mature", stats.esrbMature);
    ctx.bindValue("genre_diversity_factor", stats.genreDiversityFactor);
    ctx.bindValue("mood_uniformity_factor", stats.moodUniformity);
    ctx.bindValue("mood_sad", stats.moodSad);
    ctx.bindValue("mood_neutral", stats.moodNeutral);
    ctx.bindValue("mood_happy", stats.moodHappy);
    ctx.bindValue("crack_factor", stats.crackRatio);
    ctx.bindValue("slash_factor", stats.slashRatio);
    ctx.bindValue("smut_factor", stats.smutRatio);
    ctx.bindValue("prevalent_genre", stats.prevalentGenre);
    ctx.bindValue("size_tiny", stats.sizeFactors[0]);
    ctx.bindValue("size_medium", stats.sizeFactors[1]);
    ctx.bindValue("size_large", stats.sizeFactors[2]);
    ctx.bindValue("size_huge", stats.sizeFactors[3]);
    ctx.bindValue("first_published", stats.firstPublished);
    ctx.bindValue("last_published", stats.lastPublished);
    ctx.ExecAndCheck(true);

    return ctx.result;
}


DiagnosticSQLResult<bool> WriteAuthorFavouriteGenreStatistics(core::AuthorPtr author, QSqlDatabase db)
{
    QString query = "INSERT INTO AuthorFavouritesGenreStatistics (author_id, "
                    "General_,Humor,Poetry, Adventure, Mystery, Horror,Parody,Angst, Supernatural, Suspense, "
                    " Romance,SciFi, Fantasy,Spiritual,Tragedy, Drama, Western,Crime,Family,HurtComfort,Friendship, NoGenre) "
                    "VALUES ("
                    ":author_id, :General_,:Humor,:Poetry, :Adventure, :Mystery, :Horror,:Parody,:Angst, :Supernatural, :Suspense, "
                    " :Romance,:SciFi, :Fantasy,:Spiritual,:Tragedy, :Drama, :Western,:Crime,:Family,:HurtComfort,:Friendship, :NoGenre  "
                    ")";
    auto& genreFactors = author->stats.favouriteStats.genreFactors;

    SqlContext<bool> ctx(db, query);
    ctx.bindValue("author_id",author->id);
    auto converter = interfaces::GenreConverter::Instance();
    ctx.ProcessKeys<QString>(interfaces::GenreConverter::Instance().GetCodeGenres(), [&](auto key, auto& q){
        q.bindValue(":" + converter.ToDB(key), genreFactors[key]);
    });
    ctx.ExecAndCheck(true);
    return ctx.result;
}

DiagnosticSQLResult<bool> WriteAuthorFavouriteFandomStatistics(core::AuthorPtr author, QSqlDatabase db)
{
    QString query = "INSERT INTO AuthorFavouritesFandomRatioStatistics ("
                    "author_id, fandom_id, fandom_ratio, fic_count) "
                    "VALUES ("
                    ":author_id, :fandom_id, :fandom_ratio, :fic_count"
                    ")";

    SqlContext<bool> ctx(db, query);
    ctx.bindValue("author_id",author->id);

    ctx.ExecuteWithKeyListAndBindFunctor<int>(author->stats.favouriteStats.fandomFactorsConverted.keys(), [&](auto key, auto& q){
        q.bindValue(":fandom_id", key);
        q.bindValue(":fandom_ratio", author->stats.favouriteStats.fandomFactorsConverted[key]);
        q.bindValue(":fic_count", author->stats.favouriteStats.fandomsConverted[key]);
    });

    return ctx.result;
}


DiagnosticSQLResult<bool> WipeAuthorStatistics(core::AuthorPtr author, QSqlDatabase db)
{
    SqlContext<bool> ctx(db);
    ctx.bindValue("author_id",author->id);
    ctx.ExecuteList({"delete from AuthorFavouritesGenreStatistics where author_id = :author_id",
                     "delete from AuthorFavouritesStatistics where author_id = :author_id",
                     "delete from AuthorFavouritesFandomRatioStatistics where author_id = :author_id"
                    });
    return ctx.result;
}

DiagnosticSQLResult<QList<int>> GetAllAuthorRecommendations(int id, QSqlDatabase db)
{
    SqlContext<QList<int>> ctx(db);

    ctx.bindValue("id",id);
    ctx.FetchLargeSelectIntoList<int>("fic_id",
                                      "select fic_id from recommendations where recommender_id = :id",
                                      "select count(recommender_id) from recommendations where recommender_id = :id");
    return ctx.result;
}

DiagnosticSQLResult<QSet<int> > GetAllKnownSlashFics(QSqlDatabase db) //todo wrong table
{
    SqlContext<QSet<int>> ctx(db);
    ctx.FetchLargeSelectIntoList<int>("fic_id",
                                      "select fic_id from algopasses where keywords_pass_result = 1",
                                      "select count(fic_id) from algopasses where keywords_pass_result = 1");

    return ctx.result;
}


DiagnosticSQLResult<QSet<int> > GetAllKnownNotSlashFics(QSqlDatabase db) //todo wrong table
{
    SqlContext<QSet<int>> ctx(db);
    ctx.FetchLargeSelectIntoList<int>("fic_id",
                                      "select fic_id from algopasses where keywords_no = 1",
                                      "select count(fic_id) from algopasses where keywords_no = 1");

    return ctx.result;
}

DiagnosticSQLResult<QSet<int> > GetSingularFicsInLargeButSlashyLists(QSqlDatabase db)
{
    QString qs = QString("select fic_id from "
                         " ( "
                         " select fic_id, count(fic_id) as cnt from recommendations where recommender_id in (select author_id from AuthorFavouritesStatistics where slash_factor > 0.5 and slash_factor < 0.85  and favourites > 1000) and fic_id not in ( "
                         " select distinct fic_id from recommendations where recommender_id in (select author_id from AuthorFavouritesStatistics where slash_factor <= 0.5 or slash_factor > 0.85) ) group by fic_id "
                         " ) "
                         " where cnt = 1 ");
    SqlContext<QSet<int>> ctx(db);
    ctx.FetchLargeSelectIntoList<int>("fic_id",qs);

    return ctx.result;
}

DiagnosticSQLResult<QHash<int, double> > GetDoubleValueHashForFics(QString fieldName, QSqlDatabase db)
{
    SqlContext<QHash<int, double>> ctx(db);
    QString qs = QString("select id, %1 from fanfics order by id").arg(fieldName);
    ctx.FetchSelectIntoHash(qs, "id", fieldName);
    return ctx.result;
}

DiagnosticSQLResult<QHash<int, QString> >GetGenreForFics(QSqlDatabase db)
{
    SqlContext<QHash<int, QString>> ctx(db);
    QString qs = QString("select id, %1 from fanfics order by id").arg("genres");
    ctx.FetchSelectIntoHash(qs, "id", "genres");
    return ctx.result;
}

DiagnosticSQLResult<QHash<int, int>> GetScoresForFics(QSqlDatabase db)
{
    SqlContext<QHash<int, int>> ctx(db);
    QString qs = QString("select fic_id, score from ficscores order by fic_id asc");
    ctx.FetchSelectIntoHash(qs, "fic_id", "score");
    return ctx.result;
}

DiagnosticSQLResult<QHash<int, std::array<double, 22>>> GetGenreData(QString keyName, QString query, QSqlDatabase db)
{
    SqlContext<QHash<int, std::array<double, 22>>> ctx(db);
    ctx.FetchSelectFunctor(query, DATAQ{
                               std::size_t counter = 0;
//                               qDebug() << "Loading list for author: " << q.value(keyName).toInt();
//                               qDebug() << "Adventure value is: " << q.value("Adventure").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("General_").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Humor").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Poetry").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Adventure").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Mystery").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Horror").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Parody").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Angst").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Supernatural").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Suspense").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Romance").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("NoGenre").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("SciFi").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Fantasy").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Spiritual").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Tragedy").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Western").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Crime").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Family").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("HurtComfort").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Friendship").toDouble();
                               data[q.value(keyName).toInt()][counter++] =q.value("Drama").toDouble();
                           });

    return ctx.result;
}

DiagnosticSQLResult<QHash<int, std::array<double, 22>>> GetListGenreData(QSqlDatabase db)
{
    return GetGenreData("author_id", "select * from AuthorFavouritesGenreStatistics "
                                     " where author_id  in (select distinct recommender_id from recommendations) "
                                     " order by author_id asc", db);
}
DiagnosticSQLResult<QHash<int, std::array<double, 22> > > GetFullFicGenreData(QSqlDatabase db)
{
    return GetGenreData("fic_id", "select * from FicGenreStatistics order by fic_id", db);
}

DiagnosticSQLResult<QHash<int, double> > GetFicGenreData(QString genre, QString cutoff, QSqlDatabase db)
{
    SqlContext<QHash<int, double>> ctx(db);
    QString qs = QString("select fic_id, %1 from FicGenreStatistics where %2 order by fic_id");
    qs = qs.arg(genre).arg(cutoff);
    ctx.FetchSelectIntoHash(qs, "fic_id", genre);
    return ctx.result;
}

DiagnosticSQLResult<QSet<int> > GetAllKnownFicIds(QString where, QSqlDatabase db)
{
    SqlContext<QSet<int>> ctx(db);
    ctx.FetchLargeSelectIntoList<int>("id",
                                      "select id from fanfics where " + where,
                                      "select count(*) from fanfics where " + where);

    return ctx.result;
}
DiagnosticSQLResult<QSet<int>>  GetFicIDsWithUnsetAuthors(QSqlDatabase db)
{
    SqlContext<QSet<int>> ctx(db);
    ctx.FetchLargeSelectIntoList<int>("fic_id",
                                      "select distinct fic_id from fictags where fic_id not in (select distinct fic_id from ficauthors)");

    return ctx.result;
}

DiagnosticSQLResult<QVector<core::FicWeightPtr> > GetAllFicsWithEnoughFavesForWeights(int faves, QSqlDatabase db)
{
    SqlContext<QVector<core::FicWeightPtr>> ctx(db);
    QString qs = QString("select id,Rated, author_id, complete, updated, fandom1,fandom2,favourites, published, updated,  genres, reviews, filter_pass_1, wordcount"
                         "  from fanfics where favourites > %1 order by id").arg(QString::number(faves));
    ctx.FetchSelectFunctor(qs, DATAQ{
                               auto fw = getFicWeightPtrFromQuery(q);
                               data.push_back(fw);
                           });
    return ctx.result;
}


DiagnosticSQLResult<QHash<int, core::AuthorFavFandomStatsPtr>> GetAuthorListFandomStatistics(QList<int> authors, QSqlDatabase db)
{
    DiagnosticSQLResult<QHash<int, core::AuthorFavFandomStatsPtr>> result;
    result.data.reserve(authors.size());
    QString qs = QString(" select fandom_id, fandom_ratio, fic_count from AuthorFavouritesFandomRatioStatistics where author_id = :author_id");
    SqlContext<core::AuthorFavFandomStatsPtr> ctx(db);
    ctx.Prepare(qs);
    for(auto author: authors)
    {
        ctx.bindValue("author_id", author);
        //qDebug() << "loading author: " << author;

        core::AuthorFavFandomStatsPtr fs(new core::FandomStatsForWeightCalc);

        fs->listId = author;
        ctx.FetchSelectFunctor(qs, DATAQN{
                               fs->fandomPresence[q.value("fandom_id").toInt()] = q.value("fandom_ratio").toDouble();
                               fs->fandomCounts[q.value("fandom_id").toInt()] = q.value("fic_count").toInt();
                           }, true);
        fs->fandomCount = fs->fandomPresence.size();
        ctx.result.data = fs;

        if(!ctx.result.success)
        {
            result.oracleError = ctx.result.oracleError;
            //qDebug() << "GetAuthorListFandomStatistics: " << ctx.result.oracleError;
            result.success = false;
            result.data.clear();
            break;
        }
        result.data[author] = ctx.result.data;
    }
    return result;
}

DiagnosticSQLResult<bool> FillRecommendationListWithData(int listId,
                                                         QHash<int, int> fics,
                                                         QSqlDatabase db)
{
    QString qs = "INSERT INTO RecommendationListData ("
                 "fic_id, list_id, match_count) "
                 "VALUES ("
                 ":fic_id, :list_id, :match_count)";

    SqlContext<bool> ctx(db, qs);
    ctx.bindValue("list_id", listId);
    ctx.ExecuteWithArgsHash({"fic_id", "match_count"}, fics);
    return ctx.result;
}


DiagnosticSQLResult<bool> CreateSlashInfoPerFic(QSqlDatabase db)
{
    SqlContext<bool> ctx(db, "insert into algopasses(fic_id) select id from fanfics");
    return ctx(true);
}

DiagnosticSQLResult<bool> WipeSlashMetainformation(QSqlDatabase db)
{
    QString qs = "update algopasses set "
                 " keywords_yes = 0, keywords_no = 0, keywords_pass_result = 0, "
                 " pass_1 = 0, pass_2 = 0,pass_3 = 0,pass_4 = 0,pass_5 = 0 ";

    return SqlContext<bool>(db, qs)();
}


DiagnosticSQLResult<bool> ProcessSlashFicsBasedOnWords(std::function<SlashPresence(QString, QString, QString)> func, QSqlDatabase db)
{
    DiagnosticSQLResult<bool> result;
    result.success = false;

    CreateSlashInfoPerFic(db);
    qDebug() << "finished creating records for fics";
    WipeSlashMetainformation(db);
    qDebug() << "finished wiping slash info";

    QSet<int> slashFics;
    QSet<int> slashKeywords;
    QSet<int> notSlashFics;
    QString qs = QString("select id, summary, characters, fandom from fanfics order by id asc");
    QSqlQuery q(db);
    q.prepare(qs);
    if(!result.ExecAndCheck(q))
        return result;
    if(!result.CheckDataAvailability(q))
        return result;
    do
    {
        auto result = func(q.value("summary").toString(),
                           q.value("characters").toString(),
                           q.value("fandom").toString());
        if(result.containsSlash)
            slashKeywords.insert(q.value("id").toInt());
        if(result.IsSlash())
            slashFics.insert(q.value("id").toInt());
        if(result.containsNotSlash)
            notSlashFics.insert(q.value("id").toInt());


    }while(q.next());
    qDebug() << "finished processing slash info";
    q.prepare("update algopasses set keywords_pass_result = 1 where fic_id = :id");
    QList<int> list = slashFics.values();
    int counter = 0;
    for(auto tempId: list)
    {
        counter++;
        q.bindValue(":id", tempId);
        if(!result.ExecAndCheck(q))
        {
            qDebug() << "failed to write slash";
            return result;
        }
    }
    q.prepare("update algopasses set keywords_no = 1 where fic_id = :id");
    list = notSlashFics.values();
    counter = 0;
    for(auto tempId: list)
    {
        counter++;
        q.bindValue(":id", tempId);
        if(!result.ExecAndCheck(q))
        {
            qDebug() << "failed to write slash";
            return result;
        }
    }
    q.prepare("update algopasses set keywords_yes = 1 where fic_id = :id");
    list = slashKeywords.values();
    counter = 0;
    for(auto tempId: list)
    {
        counter++;
        q.bindValue(":id", tempId);
        if(!result.ExecAndCheck(q))
        {
            qDebug() << "failed to write slash";
            return result;
        }
    }
    qDebug() << "finished writing slash info into DB";

    result.success = true;
    return result;
}

DiagnosticSQLResult<bool> WipeAuthorStatisticsRecords(QSqlDatabase db)
{
    QString qs = QString("delete from AuthorFavouritesStatistics");
    return SqlContext<bool>(db, qs)();
}

DiagnosticSQLResult<bool> CreateStatisticsRecordsForAuthors(QSqlDatabase db)
{
    QString qs = QString("insert into AuthorFavouritesStatistics(author_id, favourites) select r.id, (select count(*) from recommendations where recommender_id = r.id) from recommenders r");
    return SqlContext<bool>(db, qs)();
}

DiagnosticSQLResult<bool> CalculateSlashStatisticsPercentages(QString usedField,  QSqlDatabase db)
{
    QString qs = QString("update AuthorFavouritesStatistics set slash_factor  = "
                         " cast( (select count (ff.fic_id) from (select fic_id from recommendations where recommender_id = author_id) rs left join  (select fic_id, %1 from algopasses where %1 = 1) ff on ff.fic_id  = rs.fic_id) as float) "
                         "/cast( (select count (ff.fic_id) from (select fic_id from recommendations where recommender_id = author_id) rs left join  (select fic_id, %1 from algopasses)              ff on ff.fic_id  = rs.fic_id) as float)");
    qs = qs.arg(usedField);
    return SqlContext<bool>(db, qs)();
}

DiagnosticSQLResult<bool> AssignIterationOfSlash(QString iteration, QSqlDatabase db)
{
    QString qs = QString("update algopasses set %1 = 1 where keywords_pass_result = 1 or fic_id in "
                         "(select fic_id from recommendationlistdata where list_id in (select id from recommendationlists where name = 'SlashCleaned'))");
    qs = qs.arg(iteration);
    return SqlContext<bool>(db, qs)();
}

DiagnosticSQLResult<bool> WriteFicRecommenderRelationsForRecList(int list_id, QHash<uint32_t, QVector<uint32_t> > relations, QSqlDatabase db)
{
    DiagnosticSQLResult<bool> result;
    result.success = false;
    {
        QString qs = QString("delete from RecommendersForFicAndList where list_id = :list_id");
        SqlContext<bool>(db, qs,BP1(list_id))();
    }

    {
        QString qs = QString("insert into RecommendersForFicAndList(list_id, fic_id, author_id) values(:list_id, :fic_id, :author_id)");
        SqlContext<bool>ctx (db, qs);
        ctx.bindValue("list_id", list_id);
        auto fics = relations.keys();
        std::sort(fics.begin(), fics.end());
        for(auto& fic : fics){
            ctx.bindValue("fic_id", fic);
            for(auto& author : relations[fic]){
                ctx.bindValue("author_id", author);
                ctx();
                if(!ctx.result.success){
                    result = ctx.result;
                    return result;
                }
            }
        }
    }
    result.success = true;
    return result;
}

DiagnosticSQLResult<bool> WriteAuthorStatsForRecList(int list_id,
                                                            QVector<core::AuthorResult> authors,
                                                     QSqlDatabase db){
    DiagnosticSQLResult<bool> result;
    result.success = false;
    {
        QString qs = QString("delete from RecommendersForFicAndList where list_id = :list_id");
        SqlContext<bool>(db, qs,BP1(list_id))();
    }
    {
        QString qs = QString("insert into AuthorParamsForRecList"
                             "(list_id, author_id, full_list_size, total_matches, negative_matches, match_category, "
                             "list_size_without_ignores, ratio_difference_on_neutral_mood, ratio_difference_on_touchy_mood) "
                             "values(:list_id, :author_id, :full_list_size, :total_matches, :negative_matches, :match_category,"
                             ":list_size_without_ignores, :ratio_difference_on_neutral_mood, :ratio_difference_on_touchy_mood)");
        SqlContext<bool> ctx(db, qs);
        ctx.bindValue("list_id", list_id);
        for(auto& author : authors){
            ctx.bindValue("author_id", author.id);
            ctx.bindValue("full_list_size", author.fullListSize);
            ctx.bindValue("total_matches", author.matches);
            ctx.bindValue("negative_matches", author.negativeMatches);
            ctx.bindValue("match_category", static_cast<int>(author.authorMatchCloseness));
            ctx.bindValue("list_size_without_ignores", author.sizeAfterIgnore);
            ctx.bindValue("ratio_difference_on_neutral_mood", author.listDiff.neutralDifference.value_or(-1));
            ctx.bindValue("ratio_difference_on_touchy_mood", author.listDiff.touchyDifference.value_or(-1));
            ctx();
            if(!ctx.result.success){
                result = ctx.result;
                return result;
            }
        }

    }
    result.success = true;
    return result;
}



DiagnosticSQLResult<bool> PerformGenreAssignment(QSqlDatabase db)
{
    thread_local QHash<QString, int> result;
    result["General_"] = 0;
    result["Humor"] = 1;
    result["Poetry"] = 0;
    result["Adventure"] = 0;
    result["Mystery"] = 0;
    result["Horror"] = 0;
    result["Parody"] = 1;
    result["Angst"] = -1;
    result["Supernatural"] = 0;
    result["Suspense"] = 0;
    result["Romance"] = 0;
    result["NoGenre"] = 0;
    result["SciFi"] = 0;
    result["Fantasy"] = 0;
    result["Spiritual"] = 0;
    result["Tragedy"] = -1;
    result["Drama"] = -1;
    result["Western"] = 0;
    result["Crime"] = 0;
    result["Family"] = 0;
    result["HurtComfort"] = -1;
    result["Friendship"] = 1;

    QString qs = QString("update ficgenrestatistics set %1 =  CASE WHEN  (select count(distinct recommender_id) from recommendations r where r.fic_id = ficgenrestatistics .fic_id) >= 5 "
                         " THEN (select avg(%1) from AuthorFavouritesGenreStatistics afgs where afgs.author_id in (select distinct recommender_id from recommendations r where r.fic_id = ficgenrestatistics .fic_id))  "
                         " ELSE 0 END ");
    SqlContext<bool> ctx(db, qs);
    ctx.ExecuteWithArgsSubstitution(result.keys());
    return ctx.result;

}

DiagnosticSQLResult<bool> EnsureUUIDForUserDatabase(QUuid id, QSqlDatabase db)
{
    DiagnosticSQLResult<bool> result;
    QString qs;
    qs = QString("select count(*) as cnt from user_settings where name = 'db_uuid'");
    SqlContext<int> ctx(db, qs);
    ctx.FetchSingleValue<int>("cnt", 0);
    if(!ctx.Success())
    {
        result.oracleError = ctx.result.oracleError;
        return result;
    }
    result.success = true;
    if(ctx.result.data > 0)
    {

        return result;
    }
    qs = QString("insert into user_settings(name, value) values('db_uuid', '%1')");
    qs = qs.arg(id.toString());
    ctx.ReplaceQuery(qs);

    ctx();
    if(!ctx.Success())
    {
        result.oracleError = ctx.result.oracleError;
        result.success = false;
        return result;
    }
    return result;
}

DiagnosticSQLResult<bool> FillFicDataForList(int listId,
                                             const QVector<int> & fics,
                                             const QVector<int> & matchCounts,
                                             const QSet<int> &origins,
                                             QSqlDatabase db)
{
    QString qs = QString("insert into RecommendationListData(list_id, fic_id, match_count, is_origin) values(:listId, :ficId, :matchCount, :is_origin)");
    SqlContext<bool> ctx(db, qs);
    ctx.bindValue("listId", listId);
    for(int i = 0; i < fics.size(); i++)
    {
        ctx.bindValue("ficId", fics.at(i));
        ctx.bindValue("matchCount", matchCounts.at(i));
        bool isOrigin = origins.contains(fics.at(i));
        ctx.bindValue("is_origin", isOrigin);
        if(!ctx.ExecAndCheck())
        {
            ctx.result.success = false;
            break;
        }
    }
    return ctx.result;
}

//alter table RecommendationListData add column votes_common integer default 0;
//alter table RecommendationListData add column votes_rare integer default 0;
//alter table RecommendationListData add column votes_unique integer default 0;

//alter table RecommendationListData add column value_common integer default 0;
//alter table RecommendationListData add column value_rare integer default 0;
//alter table RecommendationListData add column value_unique integer default 0;

//alter table RecommendationListData add column breakdown_available integer default 0;


DiagnosticSQLResult<bool> FillFicDataForList(QSharedPointer<core::RecommendationList> list,
                                             QSqlDatabase db)
{
    QString qs = QString("insert into RecommendationListData(list_id, fic_id, match_count, is_origin, "
                         "breakdown_available,"
                         "votes_common, votes_uncommon, votes_rare, votes_unique, "
                         "value_common, value_uncommon, value_rare, value_unique, purged) "
                         "values(:listId, :ficId, :matchCount, :is_origin,"
                         ":breakdown_available,"
                         ":votes_common, :votes_uncommon, :votes_rare, :votes_unique, "
                         ":value_common, :value_uncommon, :value_rare, :value_unique, :purged)");

    SqlContext<bool> ctx(db, qs);
    //QLOG_INFO() << "Origins list: " << list->ficData.sourceFics;

    qDebug() << "Creating new vote breakdown records for list with id: " << list->id;
    ctx.bindValue("listId", list->id);
    for(int i = 0; i < list->ficData.fics.size(); i++)
    {

        int ficId = list->ficData.fics.at(i);

        ctx.bindValue("ficId", ficId);
        ctx.bindValue("purged", list->ficData.purges.at(i));
        ctx.bindValue("matchCount", list->ficData.matchCounts.at(i));
        bool isOrigin = list->ficData.sourceFics.contains(list->ficData.fics.at(i));
        //QLOG_INFO() << "Writing fic: " << ficId << " isOrigin: " << isOrigin;
        ctx.bindValue("is_origin", isOrigin);
        ctx.bindValue("breakdown_available", true);

        ctx.bindValue("votes_common", list->ficData.breakdowns[ficId].authorTypes[core::AuthorWeightingResult::EAuthorType::common]);
        ctx.bindValue("votes_uncommon", list->ficData.breakdowns[ficId].authorTypes[core::AuthorWeightingResult::EAuthorType::uncommon]);
        ctx.bindValue("votes_rare", list->ficData.breakdowns[ficId].authorTypes[core::AuthorWeightingResult::EAuthorType::rare]);
        ctx.bindValue("votes_unique", list->ficData.breakdowns[ficId].authorTypes[core::AuthorWeightingResult::EAuthorType::unique]);

        ctx.bindValue("value_common", list->ficData.breakdowns[ficId].authorTypeVotes[core::AuthorWeightingResult::EAuthorType::common]);
        ctx.bindValue("value_uncommon", list->ficData.breakdowns[ficId].authorTypeVotes[core::AuthorWeightingResult::EAuthorType::uncommon]);
        ctx.bindValue("value_rare", list->ficData.breakdowns[ficId].authorTypeVotes[core::AuthorWeightingResult::EAuthorType::rare]);
        ctx.bindValue("value_unique", list->ficData.breakdowns[ficId].authorTypeVotes[core::AuthorWeightingResult::EAuthorType::unique]);
        if(!ctx.ExecAndCheck())
        {
            ctx.result.success = false;
            break;
        }
    }
    return ctx.result;
}

DiagnosticSQLResult<QString> GetUserToken(QSqlDatabase db)
{
    QString qs = QString("Select value from user_settings where name = 'db_uuid'");

    SqlContext<QString> ctx(db, qs);
    ctx.FetchSingleValue<QString>("value", "");
    return ctx.result;
}

DiagnosticSQLResult<genre_stats::FicGenreData> GetRealGenresForFic(int ficId, QSqlDatabase db)
{
    QString query = " with count_per_fic(fic_id, ffn_id, title, genres, total, HumorComposite, Flirty, Pure_drama, Pure_Romance,"
                    " Hurty, Bondy, NeutralComposite,DramaComposite, NeutralSingle) as ("
                    " with"
                    " fic_ids as (select %1 as fid),"

                    " total as ("
                    " select fic_id, count(distinct recommender_id) as total from recommendations, fic_ids "
                    " on fic_ids.fid = recommendations.fic_id "
                    " and recommender_id in (select author_id from AuthorFavouritesStatistics where favourites > 30)"
                    " group by fic_id"
                    " )"
                    " select fanfics.id as id, fanfics.ffn_id as ffn_id,  fanfics.title as title, fanfics.genres as genres, total.total, "

                    " (select count(distinct author_id) from  AuthorMoodStatistics where Funny > 0.3 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as HumorComposite,"

                    " (select count(distinct author_id) from  AuthorMoodStatistics where Flirty > 0.5 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as Flirty,"

                    " (select count(distinct author_id) from  AuthorFavouritesGenreStatistics where (drama-romance) > 0.05 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as Pure_Drama,"

                    " (select count(distinct author_id) from  AuthorFavouritesGenreStatistics where (romance-drama) > 0.8 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as Pure_Romance, "

                    " (select count(distinct author_id) from  AuthorMoodStatistics where Hurty > 0.15 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as Hurty,"

                    " (select count(distinct author_id) from  AuthorMoodStatistics where Bondy > 0.3 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as Bondy,"

                    " (select count(distinct author_id) from  AuthorMoodStatistics where Neutral > 0.3 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as NeutralComposite,"

                    " (select count(distinct author_id) from  AuthorMoodStatistics where Dramatic > 0.3 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as DramaComposite,"


                    " (select count(distinct author_id) from  AuthorFavouritesGenreStatistics where Adventure > 0.3 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as NeutralSingle"

                    " from fanfics, total on total.fic_id = fanfics.id "
                    " )"
                    " select  fic_id, total, title, genres,"

                    " cast(HumorComposite as float)/cast(total as float) as Div_HumorComposite,"
                    " cast(Flirty as float)/cast(total as float) as Div_Flirty,"
                    " cast(DramaComposite as float)/cast(total as float) as Div_Dramatic,"
                    " cast(Pure_drama as float)/cast(total as float) as Div_PureDrama,"
                    " cast(Pure_Romance as float)/cast(total as float) as Div_PureRomance,"
                    " cast(Bondy as float)/cast(total as float) as Div_Bondy,"
                    " cast(Hurty as float)/cast(total as float) as Div_Hurty,"
                    " cast(NeutralComposite as float)/cast(total as float) as Div_NeutralComposite,"
                    " cast(NeutralSingle as float)/cast(total as float) as Div_NeutralSingle,"

                    " ffn_id "

                    " from count_per_fic";
    query= query.arg(QString::number(ficId));
    SqlContext<genre_stats::FicGenreData> ctx(db, query);
    auto genreConverter = interfaces::GenreConverter::Instance();

    ctx.ForEachInSelect([&](QSqlQuery& q){
        ctx.result.data.originalGenreString = q.value("genres").toString();
        ctx.result.data.originalGenres = genreConverter.GetFFNGenreList(q.value("genres").toString());

        ctx.result.data.ficId = ficId;
        ctx.result.data.ffnId = q.value("ffn_id").toInt();
        ctx.result.data.totalLists = q.value("total").toInt();

        ctx.result.data.strengthHumor = q.value("Div_HumorComposite").toFloat();
        ctx.result.data.strengthRomance = q.value("Div_Flirty").toFloat();
        ctx.result.data.strengthDrama= q.value("Div_Dramatic").toFloat();
        ctx.result.data.strengthBonds= q.value("Div_Bondy").toFloat();
        ctx.result.data.strengthHurtComfort= q.value("Div_Hurty").toFloat();
        ctx.result.data.strengthNeutralComposite= q.value("Div_NeutralComposite").toFloat();
        ctx.result.data.strengthNeutralAdventure= q.value("Div_NeutralSingle").toFloat();
    });
    return ctx.result;
}

DiagnosticSQLResult<QHash<uint32_t, genre_stats::ListMoodData>> GetMoodDataForLists(QSqlDatabase db)
{
    QString query = " select * from AuthorMoodStatistics "
                    " where author_id in (select distinct recommender_id from recommendations) "
                    " order by author_id asc ";

    SqlContext<QHash<uint32_t, genre_stats::ListMoodData>> ctx(db, query);
    auto genreConverter = interfaces::GenreConverter::Instance();
    genre_stats::ListMoodData tmp;
    ctx.ForEachInSelect([&](QSqlQuery& q){
        tmp.listId = q.value("author_id").toInt();

        tmp.strengthBondy = q.value("bondy").toFloat();
        tmp.strengthDramatic= q.value("dramatic").toFloat();
        tmp.strengthFlirty= q.value("flirty").toFloat();
        tmp.strengthFunny= q.value("funny").toFloat();
        tmp.strengthHurty= q.value("hurty").toFloat();
        tmp.strengthNeutral= q.value("neutral").toFloat();

        tmp.strengthNonBondy= q.value("nonbondy").toFloat();
        tmp.strengthNonDramatic= q.value("NonDramatic").toFloat();
        tmp.strengthNonFlirty= q.value("NonFlirty").toFloat();
        tmp.strengthNonFunny= q.value("NonFunny").toFloat();
        tmp.strengthNonHurty= q.value("NonHurty").toFloat();
        tmp.strengthNonNeutral= q.value("NonNeutral").toFloat();
        tmp.strengthNonShocky= q.value("NonShocky").toFloat();
        tmp.strengthNone= q.value("None").toFloat();
        tmp.strengthOther= q.value("Other").toFloat();


        ctx.result.data.insert(tmp.listId, tmp);
    });
    return ctx.result;
}

DiagnosticSQLResult<QVector<genre_stats::FicGenreData>> GetGenreDataForQueuedFics(QSqlDatabase db)
{
    QString query = " with count_per_fic(fic_id, ffn_id, title, genres, total, HumorComposite, Flirty, Pure_drama, Pure_Romance,"
                    " Hurty, Bondy, NeutralComposite,DramaComposite, NeutralSingle) as ("
                    " with"
                    " fic_ids as (select id as fid from fanfics where queued_for_action = 1),"

                    " total as ("
                    " select fic_id, count(distinct recommender_id) as total from recommendations, fic_ids "
                    " on fic_ids.fid = recommendations.fic_id "
                    " and recommender_id in (select author_id from AuthorFavouritesStatistics where favourites > 30)"
                    " group by fic_id"
                    " )"
                    " select fanfics.id as id, fanfics.ffn_id as ffn_id,  fanfics.title as title, fanfics.genres as genres, total.total, "

                    " (select count(distinct author_id) from  AuthorMoodStatistics where Funny > 0.3 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as HumorComposite,"

                    " (select count(distinct author_id) from  AuthorMoodStatistics where Flirty > 0.5 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as Flirty,"

                    " (select count(distinct author_id) from  AuthorFavouritesGenreStatistics where (drama-romance) > 0.05 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as Pure_Drama,"

                    " (select count(distinct author_id) from  AuthorFavouritesGenreStatistics where (romance-drama) > 0.8 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as Pure_Romance, "

                    " (select count(distinct author_id) from  AuthorMoodStatistics where Hurty > 0.15 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as Hurty,"

                    " (select count(distinct author_id) from  AuthorMoodStatistics where Bondy > 0.3 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as Bondy,"

                    " (select count(distinct author_id) from  AuthorMoodStatistics where Neutral > 0.3 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as NeutralComposite,"

                    " (select count(distinct author_id) from  AuthorMoodStatistics where Dramatic > 0.3 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as DramaComposite,"


                    " (select count(distinct author_id) from  AuthorFavouritesGenreStatistics where Adventure > 0.3 and author_id in "
                    " (select distinct recommender_id from recommendations where fanfics.id = fic_id)) as NeutralSingle"

                    " from fanfics, total on total.fic_id = fanfics.id "
                    " )"
                    " select  fic_id, total, title, genres,"

                    " cast(HumorComposite as float)/cast(total as float) as Div_HumorComposite,"
                    " cast(Flirty as float)/cast(total as float) as Div_Flirty,"
                    " cast(DramaComposite as float)/cast(total as float) as Div_Dramatic,"
                    " cast(Pure_drama as float)/cast(total as float) as Div_PureDrama,"
                    " cast(Pure_Romance as float)/cast(total as float) as Div_PureRomance,"
                    " cast(Bondy as float)/cast(total as float) as Div_Bondy,"
                    " cast(Hurty as float)/cast(total as float) as Div_Hurty,"
                    " cast(NeutralComposite as float)/cast(total as float) as Div_NeutralComposite,"
                    " cast(NeutralSingle as float)/cast(total as float) as Div_NeutralSingle,"

                    " ffn_id "

                    " from count_per_fic";

    SqlContext<QVector<genre_stats::FicGenreData>> ctx(db, query);
    auto genreConverter = interfaces::GenreConverter::Instance();
    genre_stats::FicGenreData tmp;
    ctx.ForEachInSelect([&](QSqlQuery& q){
        tmp.originalGenreString = q.value("genres").toString();
        tmp.originalGenres = genreConverter.GetFFNGenreList(q.value("genres").toString());

        tmp.ficId = q.value("fic_id").toInt();
        tmp.ffnId = q.value("ffn_id").toInt();
        tmp.totalLists = q.value("total").toInt();

        tmp.strengthHumor = q.value("Div_HumorComposite").toFloat();
        tmp.strengthRomance = q.value("Div_Flirty").toFloat();
        tmp.strengthDrama= q.value("Div_Dramatic").toFloat();
        tmp.strengthBonds= q.value("Div_Bondy").toFloat();
        tmp.strengthHurtComfort= q.value("Div_Hurty").toFloat();
        tmp.strengthNeutralComposite= q.value("Div_NeutralComposite").toFloat();
        tmp.strengthNeutralAdventure= q.value("Div_NeutralSingle").toFloat();
        ctx.result.data.push_back(tmp);
    });
    return ctx.result;
}

DiagnosticSQLResult<bool> QueueFicsForGenreDetection(int minAuthorRecs, int minFoundLists, int minFaves, QSqlDatabase db)
{
    QString qs = QString(" with "
                         " min_recs(val) as (select :minAuthorRecs), "
                         " min_filtered_lists(val) as (select :minFoundLists), "
                         " filtered_recommenders(filtered_rec) as( "
                         " select author_id from AuthorFavouritesStatistics where favourites >= (select val from min_recs)  "
                         " ), "
                         " to_update(fic_id) as "
                         " ( "
                         "  select fic_id from ( "
                         "  select fic_id, count(fic_id) as count_rec from recommendations where recommender_id in filtered_recommenders group by fic_id "
                         "  ) fin where count_rec >= (select val from min_filtered_lists) "
                         " ) "
                         " update fanfics set queued_for_action = 1 where id in to_update and favourites > %1 ");
    qs=qs.arg(minFaves);

    return SqlContext<bool> (db, qs,BP2(minAuthorRecs,minFoundLists))();
}










//DiagnosticSQLResult<bool> FillAuthorDataForList(int listId, const QVector<int> &, QSqlDatabase db)
//{
//    QString qs = QString("insert into RecommendationListAuthorStats(list_id, author_id, match_count) values(:listId, :ficId, :matchCount)");
//    SqlContext<bool> ctx(db, qs);
//    ctx.bindValue("listId", listId);
//    for(int i = 0; i < fics.size(); i++)
//    {
//        ctx.bindValue("ficId", fics.at(i));
//        ctx.bindValue("matchCount", matchCounts.at(i));
//        if(!ctx.ExecAndCheck())
//        {
//            ctx.result.success = false;
//            break;
//        }
//    }
//    return ctx.result;
//}








}

}



//bool RebindFicsToIndex(int oldId, int newId, QSqlDatabase db)
//{
//    QString qs = QString("update ficfandoms set fandom_id = :new_id, reassigned = 1 where fandom_id = :old_id and reassigned != 1");

//    QSqlQuery q(db);
//    q.prepare(qs);
//    q.bindValue(":old_id", oldId);
//    q.bindValue(":new_id", newId);
//    if(q.lastError().isValid() && !q.lastError().text().contains("UNIQUE constraint failed"))
//    {
//        qDebug() << q.lastError();
//        qDebug() << q.lastQuery();
//        return false;
//    }
//    return true;
//}


