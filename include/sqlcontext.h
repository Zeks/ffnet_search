#pragma once
#include "transaction.h"
#include "logger/QsLog.h"
#include <fmt/format.h>

#include <QString>
#include <functional>
#include <memory>
#include <array>
#include <unordered_map>
#include "sql_abstractions/string_hasher.h"



namespace database {
namespace puresql{

struct QueryBinding{
    std::string key;
    QVariant value;
};


bool ExecAndCheck(sql::Query& q, bool reportErrors = true,  bool ignoreUniqueness = false);

template <typename T>
struct DiagnosticSQLResult
{
    bool success = true;
    QString oracleError;
    T data;
    bool ExecAndCheck(sql::Query& q, bool ignoreUniqueness = false) {
        bool success = sql::ExecAndCheck(q, true, ignoreUniqueness);
        bool uniqueTriggered = ignoreUniqueness && q.lastError().text().contains(QStringLiteral("UNIQUE constraint failed"));
        if(uniqueTriggered)
            return true;
        if(!success && !uniqueTriggered)
        {
            success = false;
            oracleError = q.lastError().text();
            qDebug().noquote() << oracleError;
            qDebug() << q.lastQuery();
        }
        return success;
    }
    bool CheckDataAvailability(sql::Query& q, bool allowEmptyRecords = false){
        if(!q.next())
        {
            if(!allowEmptyRecords)
            {
                success = false;
                oracleError = QStringLiteral("no data to read");
            }
            return false;
        }
        return true;
    }
};


template <typename ResultType>
struct SqlContext
{
    SqlContext(sql::Database db) : q(db), transaction(db){
    }
    SqlContext(sql::Database db, std::string&& qs) :qs(qs), q(db), transaction(db) {
        Prepare(qs);
    }

    SqlContext(sql::Database db, std::list<std::string>&& queries) : q(db), transaction(db)
    {
        for(const auto& query : queries)
        {
            Prepare(query);
            BindValues();
            ExecAndCheck();
        }
    }

    SqlContext(sql::Database db, std::string&& qs,  std::function<void(SqlContext<ResultType>*)> func) : qs(qs), q(db), transaction(db),  func(func){
        Prepare(qs);
        func(this);
    }

    SqlContext(sql::Database db, std::string&& qs, std::unordered_map<std::string, QVariant>&& hash) :  qs(qs), q(db),  transaction(db){
        Prepare(qs);

        for(auto i = hash.begin(); i != hash.end(); i++)
            bindValue(i->first, std::move(i->second));
    }

    SqlContext(sql::Database db, std::unordered_map<std::string, QVariant>&& hash) :  q(db),  transaction(db){
        for(auto i = hash.begin(); i != hash.end(); i++)
            bindValue(i->first, std::move(i->second));
    }

    ~SqlContext(){
        if(!result.success)
            transaction.cancel();
        else
            transaction.finalize();
    }

    DiagnosticSQLResult<ResultType> operator()(bool ignoreUniqueness = false){
        BindValues();
        ExecAndCheck(ignoreUniqueness);
        return result;
    }

    void ReplaceQuery(std::string&& query){
        qs = query;
        Prepare(qs);
        bindValues.clear();
    }

    void ExecuteWithArgsSubstitution(std::list<std::string>&& keys){
        for(const auto& key : keys)
        {
            auto newString = qs;
            newString = fmt::format(newString, key);
            Prepare(newString);
            BindValues();
            ExecAndCheck();
            if(!result.success)
                break;
        }
    }

    template <typename HashKey, typename HashValue>
    void ExecuteWithArgsHash(QStringList nameKeys, QHash<HashKey, HashValue> args, bool ignoreUniqueness = false){
        BindValues();
        for(const auto& key : args.keys())
        {
            q.bindValue(":" + nameKeys[0], key);
            q.bindValue(":" + nameKeys[1], args[key]);
            if(!ExecAndCheck(ignoreUniqueness))
            {
                qDebug() << "breaking out of cycle";
                break;
            }
        }
    }
    template <typename KeyType>
    void ExecuteWithKeyListAndBindFunctor(QList<KeyType> keyList, std::function<void(KeyType&& key, sql::Query& q)>&& functor, bool ignoreUniqueness = false){
        BindValues();
        for(auto&& key : keyList)
        {
            functor(std::move(key), q);
            if(!ExecAndCheck(ignoreUniqueness))
                break;
        }
    }

    template <typename ValueType>
    void ExecuteWithValueList(QString keyName, QList<ValueType>&& valueList, bool ignoreUniqueness = false){
        BindValues();
        for(auto&& value : valueList)
        {
            q.bindValue(":" + keyName, std::move(value));
            if(!ExecAndCheck(ignoreUniqueness))
                break;
        }
    }
    template <typename ValueType>
    void ExecuteWithValueList(QString keyName, QVector<ValueType> valueList, bool ignoreUniqueness = false){
        BindValues();
        for(auto value : valueList)
        {
            q.bindValue(":" + keyName, value);
            if(!ExecAndCheck(ignoreUniqueness))
                break;
        }
    }


    bool ExecAndCheck(bool ignoreUniqueness = false){
        BindValues();
        return result.ExecAndCheck(q, ignoreUniqueness);
    }
    bool CheckDataAvailability(bool allowEmptyRecords = false){
        return result.CheckDataAvailability(q, allowEmptyRecords);
    }
    bool ExecAndCheckForData(){
        BindValues();
        result.ExecAndCheck(q);
        if(!result.success)
            return false;
        result.CheckDataAvailability(q);
        if(!result.success)
            return false;
        return true;
    }
    void FetchSelectFunctor(std::string&& select, std::function<void(ResultType& data, sql::Query& q)>&& f, bool allowEmptyRecords = false)
    {
        Prepare(select);
        BindValues();

        if(!ExecAndCheck())
            return;

        if(!CheckDataAvailability(allowEmptyRecords))
            return;

        do{
            f(result.data, q);
        } while(q.next());
    }
    template <typename ValueType>
    void FetchLargeSelectIntoList(std::string&& fieldName, std::string&& actualQuery, std::string&& countQuery = "",
                                  std::function<ValueType(sql::Query&)>&& func = std::function<ValueType(sql::Query&)>())
    {
        if(countQuery.length() == 0)
            qs = "select count(*) from ( " + actualQuery + " ) as aliased_count";
        else
            qs = countQuery;
        Prepare(qs);
        if(!ExecAndCheck())
            return;

        if(!CheckDataAvailability())
            return;
        int size = q.value(0).toInt();
        //qDebug () << "query size: " << size;
        if(size == 0)
            return;
        result.data.reserve(size);

        qs = actualQuery;
        Prepare(qs);
        //BindValues();

        if(!ExecAndCheck())
            return;
        if(!CheckDataAvailability())
            return;

        do{
            if(!func)
                result.data += q.value(fieldName.c_str()).template value<typename ResultType::value_type>();
            else
                result.data += func(q);
        } while(q.next());
    }

    template <typename ValueType>
    void FetchLargeSelectIntoListWithoutSize(std::string&& fieldName, std::string&& actualQuery,
                                  std::function<ValueType(sql::Query&)>&& func = std::function<ValueType(sql::Query&)>())
    {
        qs = actualQuery;
        Prepare(qs);

        if(!ExecAndCheck())
            return;
        if(!CheckDataAvailability())
            return;

        do{
            if(!func)
                result.data += q.value(fieldName.c_str()).template value<typename ResultType::value_type>();
            else
                result.data += func(q);
        } while(q.next());
    }

    void FetchSelectIntoHash(std::string&& actualQuery, std::string&& idFieldName, std::string&& valueFieldName)
    {
        qs = actualQuery;
        Prepare(qs);
        BindValues();


        if(!ExecAndCheck())
            return;
        if(!CheckDataAvailability())
            return;
        do{
            result.data[q.value(idFieldName.c_str()).template value<typename ResultType::key_type>()] =  q.value(valueFieldName.c_str()).template value<typename ResultType::mapped_type>();
        } while(q.next());
    }

    template <typename T>
    void FetchSingleValue(std::string&& valueName,
                          ResultType defaultValue,
                          bool requireExisting = true,
                          std::string&& select = ""
            ){
        result.data = defaultValue;
        if(select.length() != 0)
        {
            qs = select;
            Prepare(qs);
            BindValues();
        }
        if(!ExecAndCheck())
            return;
        if(!CheckDataAvailability())
        {
            if(!requireExisting)
                result.success = true;
            return;
        }
        result.data = q.value(valueName.c_str()).template value<T>();
    }

    void ExecuteList(std::list<std::string>&& queries){
        bool execResult = true;
        for(const auto& query : queries)
        {
            Prepare(query);
            BindValues();
            if(!ExecAndCheck())
            {
                execResult = false;
                break;
            }
        }
        result.data = execResult;
    }

    void BindValues(){
        for(const auto& bind : std::as_const(bindValues))
        {
            q.bindValue(":" + QString::fromStdString(bind.key), bind.value);
        }
    }

    //template<typename KeyType, typename LamdaType>
    //void ProcessKeys(QList<KeyType> keys, const LamdaType& func){
    template<typename KeyType>
    void ProcessKeys(QList<KeyType> keys, const std::function<void(QString key, sql::Query&)>& func){
        for(auto key : keys)
            func(key, q);
    }

    void for_each(std::function<void(sql::Query&)> func){
        while(q.next())
            func(q);
    }

    DiagnosticSQLResult<ResultType> ForEachInSelect(const std::function<void(sql::Query&)>& func){
        BindValues();
        if(!ExecAndCheck())
            return result;
        for_each(func);
        return result;
    }
    bool Prepare(std::string_view qs){
        if(qs.length() == 0)
        {
            qDebug() << "passed empty query";
            return true;
        }
        bool success = q.prepare(QString::fromStdString(std::string(qs)));
        return success;
    }

    QVariant value(QString name){return q.value(name);}
    QString trimmedValue(QString name){return q.value(name).toString().trimmed();}

    void bindValue(std::string&& key, const QVariant& value){
        auto it = std::find_if(bindValues.begin(), bindValues.end(), [key](const QueryBinding& b){
            return b.key == key;
        });
        if(it!=bindValues.cend())
            *it=QueryBinding{it->key, value};
        else
            bindValues.emplace_back(QueryBinding{key, value});
    }
    void bindValue(const std::string& key, const QVariant& value){
        auto it = std::find_if(bindValues.begin(), bindValues.end(), [key](const QueryBinding& b){
            return b.key == key;
        });
        if(it!=bindValues.cend())
            *it = QueryBinding{it->key, value};
        else
            bindValues.emplace_back(QueryBinding{key, value});
    }

    void bindValue(const std::string& key, QVariant&& value){
        auto it = std::find_if(bindValues.begin(), bindValues.end(), [key](const QueryBinding& b){
            return b.key == key;
        });
        if(it!=bindValues.cend())
            *it = QueryBinding{it->key, value};
        else
            bindValues.emplace_back(QueryBinding{key, value});
    }
    void bindValue(std::string&& key, QVariant&& value){
        auto it = std::find_if(bindValues.begin(), bindValues.end(), [key](const QueryBinding& b){
            return b.key == key;
        });
        if(it!=bindValues.cend())
            *it = QueryBinding{it->key, value};
        else
            bindValues.emplace_back(QueryBinding{key, value});
    }
    void SetDefaultValue(ResultType value) {result.data = value;}
    bool Success() const {return result.success;}
    bool Next() { return q.next();}
    DiagnosticSQLResult<ResultType> result;
    std::string qs;
    sql::Query q;
    Transaction transaction;
    std::list<QueryBinding> bindValues;
    std::function<void(SqlContext<ResultType>*)> func;
};




template <typename ResultType>
struct ParallelSqlContext
{
    ParallelSqlContext(sql::Database source, std::string&& sourceQuery, QList<std::string>&& sourceFields,
                       sql::Database target, std::string&& targetQuery, QList<std::string>&& targetFields):
        sourceQ(source), targetQ(target),
        sourceDB(source), targetDB(target), transaction(target) {
        sourceQ.prepare(QString::fromStdString(sourceQuery));
        targetQ.prepare(QString::fromStdString(targetQuery));
        this->sourceFields = sourceFields;
        this->targetFields = targetFields;
    }

    ~ParallelSqlContext(){
        if(!result.success)
            transaction.cancel();
    }

    DiagnosticSQLResult<ResultType> operator()(bool ignoreUniqueness = false){
        if(!result.ExecAndCheck(sourceQ))
            return result;

        QVariant value;
        while(sourceQ.next())
        {
            for(int i = 0; i < sourceFields.size(); ++i )
            {
                //qDebug() << "binding field: " << sourceFields[i];
                if(valueConverters.contains(sourceFields[i]))
                {
                    value = valueConverters.value(sourceFields.at(i))(sourceFields.at(i), sourceQ, targetDB, result);
                    if(!result.success)
                        return result;
                }
                else
                {
                    value = sourceQ.value(sourceFields.at(i).c_str());
                    //qDebug() << "binding value: " << value;
                }
                //qDebug() << "to target field: " << targetFields[i];
                targetQ.bindValue((":" + targetFields[i]).c_str(), value);
            }

            if(!result.ExecAndCheck(targetQ, ignoreUniqueness))
                return result;
        }
        return result;
    }
    bool Success() const {return result.success;}
    DiagnosticSQLResult<ResultType> result;
    sql::Query sourceQ;
    sql::Query targetQ;
    sql::Database sourceDB;
    sql::Database targetDB;
    QList<std::string> sourceFields;
    QList<std::string> targetFields;
    Transaction transaction;
    QHash<std::string,std::function<QVariant(const std::string&, sql::Query, sql::Database, DiagnosticSQLResult<ResultType>&)>> valueConverters;
};

}
}
