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
#include "ui/mainwindow.h"
#include "ui/initialsetupdialog.h"
#include "Interfaces/db_interface.h"
#include "Interfaces/interface_sqlite.h"
#include "include/sqlitefunctions.h"
#include "include/pure_sql.h"
#include "include/db_fixers.h"

#include <QApplication>
#include <QDir>
#include <QDebug>
#include <QSettings>
#include <QTextCodec>
#include <QStandardPaths>
#include <QMessageBox>
void SetupLogger()
{
    QSettings settings("settings/settings.ini", QSettings::IniFormat);

    An<QsLogging::Logger> logger;
    logger->setLoggingLevel(static_cast<QsLogging::Level>(settings.value("Logging/loglevel").toInt()));
    QString logFile = settings.value("Logging/filename").toString();
    QsLogging::DestinationPtr fileDestination(

                QsLogging::DestinationFactory::MakeFileDestination(logFile,
                                                                   settings.value("Logging/rotate", true).toBool(),
                                                                   settings.value("Logging/filesize", 512).toInt()*1000000,
                                                                   settings.value("Logging/amountOfFilesToKeep", 50).toInt()));

    QsLogging::DestinationPtr debugDestination(
                QsLogging::DestinationFactory::MakeDebugOutputDestination() );
    logger->addDestination(debugDestination);
    logger->addDestination(fileDestination);
}



database::puresql::DiagnosticSQLResult<database::puresql::DBVerificationResult> VerifyDatabase(QString name){

    database::puresql::DiagnosticSQLResult<database::puresql::DBVerificationResult>  result;
    auto db = QSqlDatabase::addDatabase("QSQLITE","TEST");
    db.setDatabaseName(name);
    bool open = db.open();
    if(!open)
    {
        result.success = false;
        result.data.data.push_back("Database file failed to open");
        return result;
    }
    result = database::puresql::VerifyDatabaseIntegrity(db);
    db.close();
    return result;
}

bool ProcessBackupForInvalidDbFile(QString pathToFile, QString fileName,  QStringList error)
{
    QDir dir;
    auto backupPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/backups";
    dir.mkpath(backupPath);
    dir.setPath(backupPath);

    dir.setNameFilters({fileName + "*.sqlite"});
    auto entries = dir.entryList(QDir::NoFilter, QDir::Time|QDir::Reversed);

    bool backupRestored = false;
    QString fullFileName = pathToFile + "/" + fileName + ".sqlite";
    if(entries.size() > 0)
    {

        QFile::copy(fullFileName, pathToFile + "/" + fileName + ".sqlite.corrupted." + QDateTime::currentDateTime().toString("yyMMdd_hhmm"));
        QMessageBox::StandardButton reply;
        QString message = "Current database file is corrupted, but there is a backup in the ~User folder. Do you want to restore the backup?"
                          "\n\n"
                          "If \"No\" is selected a new database will be created.\n"
                          "If \"Yes\" a latest backup will be restored."
                          "\n\n"
                          "Corruption error:\n%1";
        auto trimmedError = error.join("\n");
        trimmedError = trimmedError.mid(0, 200);
        message = message.arg(trimmedError);
        reply = QMessageBox::question(nullptr, "Warning!", message,
                                      QMessageBox::Yes|QMessageBox::No);
        if (reply == QMessageBox::Yes)
        {
            if(fullFileName.length() > 10)
            {
                QFile::remove(fullFileName);
                QFile::copy(backupPath + "/" + entries.at(0), fullFileName);
                backupRestored = true;
            }
        }

        else{
            if(fullFileName.length() > 10)
                QFile::remove(fullFileName);
        }
    }
    else{
        if(fullFileName.length() > 10)
            QFile::remove(fullFileName);
    }
    return backupRestored;
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName("Flipper");
    SetupLogger();


    QDir dir;
    auto backupPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/backups";
    dir.mkpath(backupPath);
    dir.setPath(backupPath);

    QSharedPointer<CoreEnvironment> coreEnvironment(new CoreEnvironment());
    qDebug() << "current appPath is: " << QDir::currentPath();


    QSettings uiSettings("settings/ui.ini", QSettings::IniFormat);
    uiSettings.setIniCodec(QTextCodec::codecForName("UTF-8"));

    QString databaseFolderPath = uiSettings.value("Settings/dbPath", QCoreApplication::applicationDirPath()).toString();
    QString currentDatabaseFile = databaseFolderPath + "/" + "UserDB.sqlite";
    bool hasDBFile = QFileInfo::exists(currentDatabaseFile);

    bool backupRestored = false;
    if(databaseFolderPath.length() > 0)
    {
        auto verificationResult = VerifyDatabase(currentDatabaseFile);
        bool validDbFile  = verificationResult.success;
        if(!validDbFile)
        {
            backupRestored = ProcessBackupForInvalidDbFile(databaseFolderPath, "UserDB", verificationResult.data.data);
            if(!backupRestored)
                hasDBFile = false;

        }
    }


    MainWindow w;
    if(!hasDBFile || !uiSettings.value("Settings/initialInitComplete", false).toBool())
    {
        InitialSetupDialog setupDialog;
        setupDialog.setWindowTitle("Welcome!");
        setupDialog.setWindowModality(Qt::ApplicationModal);
        setupDialog.env = coreEnvironment;
        setupDialog.exec();
        if(!setupDialog.initComplete)
            return 0;
    }
    else
    {

        coreEnvironment->InstantiateClientDatabases(uiSettings.value("Settings/dbPath", QCoreApplication::applicationDirPath()).toString());
        coreEnvironment->InitInterfaces();
        coreEnvironment->Init();
        coreEnvironment->BackupUserDatabase();
        if(backupRestored)
            w.QueueDefaultRecommendations();
    }




    w.env = coreEnvironment;
    if(!w.Init())
        return 0;
    w.InitConnections();
    w.show();
    w.DisplayInitialFicSelection();
    w.StartTaskTimer();

    return a.exec();
}

