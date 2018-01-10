/*
FFSSE is a replacement search engine for fanfiction.net search results
Copyright (C) 2017  Marchenko Nikolai

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

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "GlobalHeaders/SingletonHolder.h"
#include "GlobalHeaders/simplesettings.h"
#include "Interfaces/recommendation_lists.h"
#include "Interfaces/authors.h"
#include "Interfaces/fandoms.h"
#include "Interfaces/fanfics.h"
#include "Interfaces/db_interface.h"
#include "Interfaces/pagetask_interface.h"
#include "pagetask.h"
#include "Interfaces/tags.h"
#include <QMessageBox>
#include <QRegExp>
#include <QDebug>
#include <QSqlQuery>
#include <QSqlDatabase>
#include <QSqlError>
#include <QPair>
#include <QPoint>
#include <QStringListModel>
#include <QDesktopServices>
#include <QTextCodec>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QQuickWidget>
#include <QDebug>
#include <QQuickView>
#include <QQuickItem>
#include <QQmlContext>
#include <QThread>
#include <QFuture>
#include <QtConcurrent>
#include <QSqlDriver>
#include <QClipboard>
#include <chrono>
#include <algorithm>

#include "genericeventfilter.h"

#include <algorithm>
#include "include/favparser.h"
#include "include/fandomparser.h"
#include "include/url_utils.h"
#include "include/pure_sql.h"
#include "include/transaction.h"

#include "Interfaces/ffn/ffn_authors.h"
#include "Interfaces/ffn/ffn_fanfics.h"
#include "Interfaces/fandoms.h"
#include "Interfaces/recommendation_lists.h"
#include "Interfaces/tags.h"
#include "Interfaces/genres.h"

struct SplitPart
{
    QString data;
    int partId;
};

struct SplitJobs
{
    QVector<SplitPart> parts;
    int favouriteStoryCountInWhole;
    int authorStoryCountInWhole;
    QString authorName;
};


QString ParseAuthorNameFromFavouritePage(QString data)
{
    QString result;
    QRegExp rx("title>([A-Za-z0-9.\\-\\s']+)(?=\\s|\\sFanFiction)");
    //rx.setMinimal(true);
    int index = rx.indexIn(data);
    if(index == -1)
        return result;
    //qDebug() << rx.capturedTexts();
    result = rx.cap(1);
    if(result.trimmed().isEmpty())
        result = rx.cap(1);
    return result;
}

SplitJobs SplitJob(QString data)
{
    SplitJobs result;
    int threadCount = QThread::idealThreadCount();
    QRegExp rxStart("<div\\sclass=\'z-list\\sfavstories\'");
    int index = rxStart.indexIn(data);
    int captured = data.count(rxStart);
    result.favouriteStoryCountInWhole = captured;

    QRegExp rxAuthorStories("<div\\sclass=\'z-list\\smystories\'");
    index = rxAuthorStories.indexIn(data);
    captured = data.count(rxAuthorStories);
    result.authorStoryCountInWhole = captured;


    qDebug() << "Will process "  << captured << " stories";

    int partSize = captured/(threadCount-1);
    qDebug() << "In packs of "  << partSize;
    index = 0;

    if(partSize < 70)
        partSize = 70;

    QList<int> splitPositions;
    int counter = 0;
    do{
        index = rxStart.indexIn(data, index+1);
        if(counter%partSize == 0 && index != -1)
        {
            splitPositions.push_back(index);
        }
        counter++;
    }while(index != -1);
    qDebug() << "Splitting into: "  << splitPositions;
    result.parts.reserve(splitPositions.size());
    for(int i = 0; i < splitPositions.size(); i++)
    {
        if(i != splitPositions.size()-1)
            result.parts.push_back({data.mid(splitPositions[i], splitPositions[i+1] - splitPositions[i]), i});
        else
            result.parts.push_back({data.mid(splitPositions[i], data.length() - splitPositions[i]),i});
    }
    return result;
}

void InsertLogIntoEditor(QTextEdit* edit, QString url)
{
    QString toInsert = "<a href=\"" + url + "\"> %1 </a>";
    toInsert= toInsert.arg(url);
    edit->append("<span>Processing url: </span>");
    if(toInsert.trimmed().isEmpty())
        toInsert=toInsert;
    edit->insertHtml(toInsert);
    QCoreApplication::processEvents();
}
bool TagEditorHider(QObject* /*obj*/, QEvent *event, QWidget* widget)
{
    if(event->type() == QEvent::FocusOut)
    {
        QWidget* focused = QApplication::focusWidget();

        if(focused->parent()->parent() != widget)
        {
            widget->hide();
            return true;
        }
    }
    return false;
}
QString NameOfFandomSectionToLink(QString val)
{
    return "https://www.fanfiction.net/" + val + "/";
}
QString NameOfCrossoverSectionToLink(QString val)
{
    return "https://www.fanfiction.net/crossovers/" + val + "/";
}
MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->cbNormals->lineEdit()->setClearButtonEnabled(true);

}

void MainWindow::Init()
{
    std::unique_ptr<core::DefaultRNGgenerator> rng (new core::DefaultRNGgenerator());
    rng->portableDBInterface = dbInterface;
    queryBuilder.SetIdRNGgenerator(rng.release());
    ui->chkShowDirectRecs->setVisible(false);
    ui->pbFirstWave->setVisible(false);


    this->setWindowTitle("ffnet sane search engine");
    QSettings settings("settings.ini", QSettings::IniFormat);
    //settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    if(settings.value("Settings/hideCache", true).toBool())
        ui->chkCacheMode->setVisible(false);

    ui->dteFavRateCut->setDate(QDate::currentDate().addDays(-366));
    ui->pbLoadDatabase->setStyleSheet("QPushButton {background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1,   stop:0 rgba(179, 229, 160, 128), stop:1 rgba(98, 211, 162, 128))}"
                                      "QPushButton:hover {background-color: #9cf27b; border: 1px solid black;border-radius: 5px;}"
                                      "QPushButton {background-color: qlineargradient(spread:pad, x1:0, y1:0, x2:0, y2:1,   stop:0 rgba(179, 229, 160, 128), stop:1 rgba(98, 211, 162, 128))}");

    //ProcessRecommendationListsFromDB(database::GetAvailableRecommendationLists());
    recsInterface->LoadAvailableRecommendationLists();

    ui->wdgTagsPlaceholder->fandomsInterface = fandomsInterface;
    tagWidgetDynamic->fandomsInterface = fandomsInterface;
    fandomsInterface->FillFandomList(true);
    recentFandomsModel = new QStringListModel;
    recommendersModel= new QStringListModel;
    ProcessTagsIntoGui();

    //QSettings settings("settings.ini", QSettings::IniFormat);
    //settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    auto storedRecList = settings.value("Settings/currentList").toString();
    qDebug() << QDir::currentPath();
    ui->cbRecGroup->setCurrentText(storedRecList);
    recsInterface->SetCurrentRecommendationList(recsInterface->GetListIdForName(storedRecList));

    qRegisterMetaType<WebPage>("WebPage");
    qRegisterMetaType<PageResult>("PageResult");
    qRegisterMetaType<ECacheMode>("ECacheMode");


    ui->edtResults->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->edtResults, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(OnShowContextMenu(QPoint)));
    connect(ui->pbWipeFandom, SIGNAL(clicked(bool)), this, SLOT(WipeSelectedFandom(bool)));
    connect(ui->pbCopyAllUrls, SIGNAL(clicked(bool)), this, SLOT(OnCopyAllUrls()));

    //    sections.insert("Anime/Manga", core::Fandom{"Anime/Manga", "anime", NameOfFandomSectionToLink("anime"), NameOfCrossoverSectionToLink("anime")});
    //    sections.insert("Misc", core::Fandom{"Misc", "misc", NameOfFandomSectionToLink("misc"), NameOfCrossoverSectionToLink("misc")});
    //    sections.insert("Books", core::Fandom{"Books", "book", NameOfFandomSectionToLink("book"), NameOfCrossoverSectionToLink("book")});
    //    sections.insert("Movies", core::Fandom{"Movies", "movie", NameOfFandomSectionToLink("movie"), NameOfCrossoverSectionToLink("movie")});
    //    sections.insert("Cartoons", core::Fandom{"Cartoons", "cartoon", NameOfFandomSectionToLink("cartoon"), NameOfCrossoverSectionToLink("cartoon")});
    //    sections.insert("Comics", core::Fandom{"Comics", "comic", NameOfFandomSectionToLink("comic"), NameOfCrossoverSectionToLink("comic")});
    //    sections.insert("Games", core::Fandom{"Games", "game", NameOfFandomSectionToLink("game"), NameOfCrossoverSectionToLink("game")});
    //    sections.insert("Plays/Musicals", core::Fandom{"Plays/Musicals", "play", NameOfFandomSectionToLink("play"), NameOfCrossoverSectionToLink("play")});
    //    sections.insert("TV Shows", core::Fandom{"TV Shows", "tv", NameOfFandomSectionToLink("tv"), NameOfCrossoverSectionToLink("tv")});

    ;
    ui->cbNormals->setModel(new QStringListModel(fandomsInterface->GetFandomList()));

    pbMain = new QProgressBar;
    pbMain->setMaximumWidth(200);
    lblCurrentOperation = new QLabel;
    lblCurrentOperation->setMaximumWidth(300);

    ui->statusBar->addPermanentWidget(lblCurrentOperation,1);
    ui->statusBar->addPermanentWidget(pbMain,0);

    ui->edtResults->setOpenLinks(false);
    auto showTagWidget = settings.value("Settings/showNewTagsWidget", false).toBool();
    if(!showTagWidget)
        ui->tagWidget->removeTab(1);
    connect(ui->edtResults, &QTextBrowser::anchorClicked, this, &MainWindow::OnLinkClicked);
    connect(ui->pbFormattedList, &QPushButton::clicked, this, &MainWindow::OnDoFormattedList);
    connect(ui->pbPauseTask, &QPushButton::clicked, [&](){
        cancelCurrentTaskPressed = true;
    });
    connect(ui->pbContinueTask, &QPushButton::clicked, [&](){
        auto task = pageTaskInterface->GetCurrentTask();
        if(!task)
        {
            QMessageBox::warning(nullptr, "Warning!", "No task to continue!");
            return;
        }
        InitUIFromTask(task);
        UseAuthorsPageTask(task, callProgress, callProgressText);
    });
    // should refer to new tag widget instead
    GenericEventFilter* eventFilter = new GenericEventFilter(this);
    eventFilter->SetEventProcessor(std::bind(TagEditorHider,std::placeholders::_1, std::placeholders::_2, tagWidgetDynamic));
    tagWidgetDynamic->installEventFilter(eventFilter);

    connect(tagWidgetDynamic, &TagWidget::tagToggled, this, &MainWindow::OnTagToggled);
    connect(ui->pbCopyFavUrls, &QPushButton::clicked, this, &MainWindow::OnCopyFavUrls);
    connect(ui->wdgTagsPlaceholder, &TagWidget::refilter, [&](){
        qwFics->rootContext()->setContextProperty("ficModel", nullptr);

        if(ui->gbTagFilters->isChecked() && ui->wdgTagsPlaceholder->GetSelectedTags().size() > 0)
        {
            filter = ProcessGUIIntoStoryFilter(core::StoryFilter::filtering_in_fics);
            LoadData();
        }
        ui->edtResults->setUpdatesEnabled(true);
        ui->edtResults->setReadOnly(true);
        holder->SetData(fanfics);
        typetableModel->OnReloadDataFromInterface();
        qwFics->rootContext()->setContextProperty("ficModel", typetableModel);
    });

    connect(ui->wdgTagsPlaceholder, &TagWidget::tagDeleted, [&](QString tag){
        ui->wdgTagsPlaceholder->OnRemoveTagFromEdit(tag);

        if(tagList.contains(tag))
        {
            tagsInterface->DeleteTag(tag);
            tagList.removeAll(tag);
            qwFics->rootContext()->setContextProperty("tagModel", tagList);
        }
    });
    connect(ui->wdgTagsPlaceholder, &TagWidget::tagAdded, [&](QString tag){
        if(!tagList.contains(tag))
        {
            tagsInterface->CreateTag(tag);
            tagList.append(tag);
            qwFics->rootContext()->setContextProperty("tagModel", tagList);
            FillRecTagBuildCombobox();
        }

    });

    connect(&taskTimer, &QTimer::timeout, this, &MainWindow::OnCheckUnfinishedTasks);

    this->setAttribute(Qt::WA_QuitOnClose);
    ReadSettings();
    SetupFanficTable();
    InitConnections();
    //fandomsInterface->RebaseFandomsToZero();

    recentFandomsModel->setStringList(fandomsInterface->GetRecentFandoms());
    ui->lvTrackedFandoms->setModel(recentFandomsModel);
    //! todo rethink
    //recommenders = database::FetchRecommenders();
    //recommendersModel->setStringList(SortedList(recommenders.keys()));
    //ui->lvRecommenders->setModel(recommendersModel);
    FillRecommenderListView();

    connect(ui->lvTrackedFandoms->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::OnNewSelectionInRecentList);
    //! todo currently null
    connect(ui->lvRecommenders->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::OnNewSelectionInRecommenderList);
    CreatePageThreadWorker();
    callProgress = [&](int counter) {
        pbMain->setTextVisible(true);
        pbMain->setFormat("%v");
        pbMain->setValue(counter);
        pbMain->show();
    };
    callProgressText = [&](QString value) {
        ui->edtResults->insertHtml(value);
        ui->edtResults->ensureCursorVisible();

    };
}

void MainWindow::InitUIFromTask(PageTaskPtr task)
{
    if(!task)
        return;
    AddToProgressLog("Authors: " + QString::number(task->size));
    ReinitProgressbar(task->size);
    DisableAllLoadButtons();
}


#define ADD_STRING_GETSET(HOLDER,ROW,ROLE,PARAM)  \
    HOLDER->AddGetter(QPair<int,int>(ROW,ROLE), \
    [] (const core::Fic* data) \
{ \
    if(data) \
    return QVariant(data->PARAM); \
    else \
    return QVariant(); \
    } \
    ); \
    HOLDER->AddSetter(QPair<int,int>(ROW,ROLE), \
    [] (core::Fic* data, QVariant value) \
{ \
    if(data) \
    data->PARAM = value.toString(); \
    } \
    ); \

#define ADD_DATE_GETSET(HOLDER,ROW,ROLE,PARAM)  \
    HOLDER->AddGetter(QPair<int,int>(ROW,ROLE), \
    [] (const core::Fic* data) \
{ \
    if(data) \
    return QVariant(data->PARAM); \
    else \
    return QVariant(); \
    } \
    ); \
    HOLDER->AddSetter(QPair<int,int>(ROW,ROLE), \
    [] (core::Fic* data, QVariant value) \
{ \
    if(data) \
    data->PARAM = value.toDateTime(); \
    } \
    ); \

#define ADD_STRING_INTEGER_GETSET(HOLDER,ROW,ROLE,PARAM)  \
    HOLDER->AddGetter(QPair<int,int>(ROW,ROLE), \
    [] (const core::Fic* data) \
{ \
    if(data) \
    return QVariant(data->PARAM); \
    else \
    return QVariant(); \
    } \
    ); \
    HOLDER->AddSetter(QPair<int,int>(ROW,ROLE), \
    [] (core::Fic* data, QVariant value) \
{ \
    if(data) \
    data->PARAM = QString::number(value.toInt()); \
    } \
    ); \

#define ADD_INTEGER_GETSET(HOLDER,ROW,ROLE,PARAM)  \
    HOLDER->AddGetter(QPair<int,int>(ROW,ROLE), \
    [] (const core::Fic* data) \
{ \
    if(data) \
    return QVariant(data->PARAM); \
    else \
    return QVariant(); \
    } \
    ); \
    HOLDER->AddSetter(QPair<int,int>(ROW,ROLE), \
    [] (core::Fic* data, QVariant value) \
{ \
    if(data) \
    data->PARAM = value.toInt(); \
    } \
    ); \

void MainWindow::SetupTableAccess()
{
    //    holder->SetColumns(QStringList() << "fandom" << "author" << "title" << "summary" << "genre" << "characters" << "rated"
    //                       << "published" << "updated" << "url" << "tags" << "wordCount" << "favourites" << "reviews" << "chapters" << "complete" << "atChapter" );
    ADD_STRING_GETSET(holder, 0, 0, fandom);
    ADD_STRING_GETSET(holder, 1, 0, author->name);
    ADD_STRING_GETSET(holder, 2, 0, title);
    ADD_STRING_GETSET(holder, 3, 0, summary);
    ADD_STRING_GETSET(holder, 4, 0, genreString);
    ADD_STRING_GETSET(holder, 5, 0, charactersFull);
    ADD_STRING_GETSET(holder, 6, 0, rated);
    ADD_DATE_GETSET(holder, 7, 0, published);
    ADD_DATE_GETSET(holder, 8, 0, updated);
    ADD_STRING_GETSET(holder, 9, 0, urlFFN);
    ADD_STRING_GETSET(holder, 10, 0, tags);
    ADD_STRING_INTEGER_GETSET(holder, 11, 0, wordCount);
    ADD_STRING_INTEGER_GETSET(holder, 12, 0, favourites);
    ADD_STRING_INTEGER_GETSET(holder, 13, 0, reviews);
    ADD_STRING_INTEGER_GETSET(holder, 14, 0, chapters);
    ADD_INTEGER_GETSET(holder, 15, 0, complete);
    ADD_INTEGER_GETSET(holder, 16, 0, atChapter);
    ADD_INTEGER_GETSET(holder, 17, 0, id);
    ADD_INTEGER_GETSET(holder, 18, 0, recommendations);


    holder->AddFlagsFunctor(
                [](const QModelIndex& index)
    {
        if(index.column() == 8)
            return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        Qt::ItemFlags result;
        result |= Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        return result;
    }
    );
}


void MainWindow::SetupFanficTable()
{
    holder = new TableDataListHolder<core::Fic>();
    typetableModel = new FicModel();

    SetupTableAccess();


    holder->SetColumns(QStringList() << "fandom" << "author" << "title" << "summary" << "genre" << "characters" << "rated" << "published"
                       << "updated" << "url" << "tags" << "wordCount" << "favourites" << "reviews" << "chapters" << "complete" << "atChapter" << "ID" << "recommendations");

    typetableInterface = QSharedPointer<TableDataInterface>(dynamic_cast<TableDataInterface*>(holder));

    typetableModel->SetInterface(typetableInterface);

    holder->SetData(fanfics);
    qwFics = new QQuickWidget();
    QHBoxLayout* lay = new QHBoxLayout;
    lay->addWidget(qwFics);
    ui->wdgFicviewPlaceholder->setLayout(lay);
    qwFics->setResizeMode(QQuickWidget::SizeRootObjectToView);
    qwFics->rootContext()->setContextProperty("ficModel", typetableModel);

    tagsInterface->LoadAlltags();
    tagList = tagsInterface->ReadUserTags();
    qwFics->rootContext()->setContextProperty("tagModel", tagList);
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    qwFics->rootContext()->setContextProperty("urlCopyIconVisible",
                                              settings.value("Settings/urlCopyIconVisible", true).toBool());
    QUrl source("qrc:/qml/ficview.qml");
    qwFics->setSource(source);

    QObject *childObject = qwFics->rootObject()->findChild<QObject*>("lvFics");
    //connect(childObject, SIGNAL(chapterChanged(QVariant, QVariant, QVariant)), this, SLOT(OnChapterUpdated(QVariant, QVariant, QVariant)));
    connect(childObject, SIGNAL(chapterChanged(QVariant, QVariant)), this, SLOT(OnChapterUpdated(QVariant, QVariant)));
    //connect(childObject, SIGNAL(tagClicked(QVariant, QVariant, QVariant)), this, SLOT(OnTagClicked(QVariant, QVariant, QVariant)));
    connect(childObject, SIGNAL(tagAdded(QVariant, QVariant)), this, SLOT(OnTagAdd(QVariant,QVariant)));
    connect(childObject, SIGNAL(tagDeleted(QVariant, QVariant)), this, SLOT(OnTagRemove(QVariant,QVariant)));
    connect(childObject, SIGNAL(urlCopyClicked(QString)), this, SLOT(OnCopyFicUrl(QString)));
    connect(childObject, SIGNAL(recommenderCopyClicked(QString)), this, SLOT(OnOpenRecommenderLinks(QString)));
    ui->deCutoffLimit->setDate(QDateTime::currentDateTime().date());
}
bool MainWindow::event(QEvent * e)
{
    switch(e->type())
    {
    case QEvent::WindowActivate :
        tagWidgetDynamic->hide();
        break ;
    default:
        break;
    } ;
    return QMainWindow::event(e) ;
}

void MainWindow::OnTagClicked(QVariant tag, QVariant currentMode, QVariant row)
{
}
MainWindow::~MainWindow()
{
    WriteSettings();
    delete ui;
}


void MainWindow::InitInterfaces()
{
    authorsInterface = QSharedPointer<interfaces::Authors> (new interfaces::FFNAuthors());
    fanficsInterface = QSharedPointer<interfaces::Fanfics> (new interfaces::FFNFanfics());
    recsInterface    = QSharedPointer<interfaces::RecommendationLists> (new interfaces::RecommendationLists());
    fandomsInterface = QSharedPointer<interfaces::Fandoms> (new interfaces::Fandoms());
    tagsInterface    = QSharedPointer<interfaces::Tags> (new interfaces::Tags());
    genresInterface  = QSharedPointer<interfaces::Genres> (new interfaces::Genres());
    pageTaskInterface= QSharedPointer<interfaces::PageTask> (new interfaces::PageTask());

    // probably need to change this to db accessor
    // to ensure db availability for later

    authorsInterface->portableDBInterface = dbInterface;
    fanficsInterface->authorInterface = authorsInterface;
    fanficsInterface->fandomInterface = fandomsInterface;
    recsInterface->portableDBInterface = dbInterface;
    recsInterface->authorInterface = authorsInterface;
    fandomsInterface->portableDBInterface = dbInterface;
    tagsInterface->fandomInterface = fandomsInterface;

    //bool isOpen = dbInterface->GetDatabase().isOpen();
    authorsInterface->db = dbInterface->GetDatabase();
    fanficsInterface->db = dbInterface->GetDatabase();
    recsInterface->db    = dbInterface->GetDatabase();
    fandomsInterface->db = dbInterface->GetDatabase();
    tagsInterface->db    = dbInterface->GetDatabase();
    genresInterface->db  = dbInterface->GetDatabase();
    queryBuilder.portableDBInterface = dbInterface;
    pageTaskInterface->db  = tasksInterface->GetDatabase();
    fandomsInterface->Load();
}

void MainWindow::InitConnections()
{
    connect(ui->chkCustomFilter, &QCheckBox::clicked, this, &MainWindow::OnCustomFilterClicked);
    connect(ui->chkActivateReloadSectionData, &QCheckBox::clicked, this, &MainWindow::OnSectionReloadActivated);
    connect(ui->chkShowDirectRecs, &QCheckBox::clicked, this, &MainWindow::OnReloadRecLists);

}

void MainWindow::RequestAndProcessPage(QString fandom, QDate lastFandomUpdatedate, QString page)
{
    nextUrl = page;
    //qDebug() << "will request url:" << nextUrl;
    if(ui->cbUseDateCutoff->isChecked())
        lastFandomUpdatedate = ui->deCutoffLimit->date();
    if(ui->chkIgnoreUpdateDate->isChecked())
        lastFandomUpdatedate = QDate();

    StartPageWorker();
    DisableAllLoadButtons();

    An<PageManager> pager;
    auto cacheMode = ui->chkCacheMode->isChecked() ? ECacheMode::use_cache : ECacheMode::dont_use_cache;
    qDebug() << "will request url:" << nextUrl;
    WebPage currentPage = pager->GetPage(nextUrl, cacheMode);
    FandomParser parser(fanficsInterface);
    QString lastUrl = parser.GetLast(currentPage.content);
    int pageCount = lastUrl.mid(lastUrl.lastIndexOf("=")+1).toInt();
    if(pageCount != 0)
    {
        pbMain->show();
        pbMain->setMaximum(pageCount);
    }
    qDebug() << "emitting page task:" << nextUrl << "\n" << lastUrl << "\n" << lastFandomUpdatedate;
    emit pageTask(nextUrl, lastUrl, lastFandomUpdatedate, ui->chkCacheMode->isChecked() ? ECacheMode::use_cache : ECacheMode::dont_use_cache);
    int counter = 0;
    WebPage webPage;
    QSqlDatabase db = QSqlDatabase::database();

    QSet<QString> updatedFandoms;
    database::Transaction transaction(db);
    do
    {

        while(pageQueue.pending && pageQueue.data.isEmpty())
        {
            QThread::msleep(500);
            //qDebug() << "worker value is: " << worker->value;
            if(!worker->working)
                pageThread.start(QThread::HighPriority);
            QCoreApplication::processEvents();
        }
        if(!pageQueue.pending && pageQueue.data.isEmpty())
            break;

        webPage = pageQueue.data.at(0);
        pageQueue.data.pop_front();
        webPage.crossover = webPage.url.contains("Crossovers");
        webPage.fandom =  fandom;
        webPage.type = EPageType::sorted_ficlist;
        auto startPageProcessing= std::chrono::high_resolution_clock::now();
        parser.ProcessPage(webPage);
        auto elapsed = std::chrono::high_resolution_clock::now() - startPageProcessing;
        qDebug() << "Page processed in: " << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        if(webPage.source == EPageSource::network)
            pager->SavePageToDB(webPage);

        QCoreApplication::processEvents();

        if(pageCount == 0)
            pbMain->setValue((pbMain->value()+10)%pbMain->maximum());
        else
            pbMain->setValue(counter++);



        auto startPageRequest = std::chrono::high_resolution_clock::now();

        {
            auto startQueue= std::chrono::high_resolution_clock::now();

            fanficsInterface->ProcessIntoDataQueues(parser.processedStuff);
            auto elapsedQueue = std::chrono::high_resolution_clock::now() - startQueue;
            qDebug() << "Queue processed in: " << std::chrono::duration_cast<std::chrono::microseconds>(elapsedQueue).count();
            auto startFandoms= std::chrono::high_resolution_clock::now();
            auto fandoms = fandomsInterface->EnsureFandoms(parser.processedStuff);
            auto elapsedFandoms = std::chrono::high_resolution_clock::now() - startFandoms;
            qDebug() << "Fandoms processed in: " << std::chrono::duration_cast<std::chrono::microseconds>(elapsedFandoms).count();
            updatedFandoms.intersect(fandoms);

            auto startFlush= std::chrono::high_resolution_clock::now();
            fanficsInterface->FlushDataQueues();
            auto elapsedFlush= std::chrono::high_resolution_clock::now() - startFlush;
            qDebug() << "Flush processed in: " << std::chrono::duration_cast<std::chrono::microseconds>(elapsedFlush).count();
        }
        processedFics+=parser.processedStuff.size();

        elapsed = std::chrono::high_resolution_clock::now() - startPageRequest;
        qDebug() << "Written into Db in: " << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

        if(parser.minSectionUpdateDate < lastFandomUpdatedate && !ui->chkIgnoreUpdateDate->isChecked())
        {
            ui->edtResults->append("Already have updates past this point. Aborting.");
            break;
        }
        if(webPage.isLastPage)
            break;

    }while(!webPage.isLastPage);
    fandomsInterface->RecalculateFandomStats(updatedFandoms.values());
    transaction.finalize();
    StopPageWorker();
    ShutdownProgressbar();
    EnableAllLoadButtons();
    //manager.get(QNetworkRequest(QUrl(page)));
}

WebPage MainWindow::RequestPage(QString pageUrl, ECacheMode cacheMode, bool autoSaveToDB)
{
    WebPage result;
    QString toInsert = "<a href=\"" + pageUrl + "\"> %1 </a>";
    toInsert= toInsert.arg(pageUrl);
    ui->edtResults->append("<span>Processing url: </span>");
    if(toInsert.trimmed().isEmpty())
        toInsert=toInsert;
    ui->edtResults->insertHtml(toInsert);

    //auto cacheMode = ui->chkCacheMode->isChecked() ? ECacheMode::use_cache : ECacheMode::dont_use_cache;

    pbMain->setTextVisible(false);
    pbMain->show();
    An<PageManager> pager;
    pager->SetDatabase(QSqlDatabase::database());
    result = pager->GetPage(pageUrl, cacheMode);
    if(autoSaveToDB)
        pager->SavePageToDB(result);
    return result;
}



inline core::Fic LoadFanfic(QSqlQuery& q)
{
    core::Fic result;
    result.id = q.value("ID").toInt();
    result.fandom = q.value("FANDOM").toString();
    result.author = core::Author::NewAuthor();
    result.author->name = q.value("AUTHOR").toString();
    result.title = q.value("TITLE").toString();
    result.summary = q.value("SUMMARY").toString();
    result.genreString = q.value("GENRES").toString();
    result.charactersFull = q.value("CHARACTERS").toString().replace("not found", "");
    result.rated = q.value("RATED").toString();
    auto published = q.value("PUBLISHED").toDateTime();
    auto updated   = q.value("UPDATED").toDateTime();
    result.published = published;
    //result.updated= updated.date().year() > 1970 ? updated : published;
    result.updated= updated;
    result.SetUrl("ffn",q.value("URL").toString());
    result.tags = q.value("TAGS").toString();
    result.wordCount = q.value("WORDCOUNT").toString();
    result.favourites = q.value("FAVOURITES").toString();
    result.reviews = q.value("REVIEWS").toString();
    result.chapters = q.value("CHAPTERS").toString();
    result.complete= q.value("COMPLETE").toInt();
    result.atChapter = q.value("AT_CHAPTER").toInt();
    result.recommendations= q.value("SUMRECS").toInt();
    //result.recommendations= 1;
    return result;
}

void MainWindow::LoadData()
{
    if(ui->cbMinWordCount->currentText().trimmed().isEmpty())
    {
        QMessageBox::warning(0, "warning!", "Please set minimum word count");
        return;
    }
    auto q = BuildQuery();
    q.setForwardOnly(true);
    q.exec();
    qDebug() << q.lastQuery();
    if(q.lastError().isValid())
    {
        qDebug() << q.lastError();
        qDebug() << q.lastQuery();
    }
    ui->edtResults->setOpenExternalLinks(true);
    ui->edtResults->clear();
    ui->edtResults->setFont(QFont("Verdana", 20));
    int counter = 0;
    ui->edtResults->setUpdatesEnabled(false);
    fanfics.clear();
    //ui->edtResults->insertPlainText(q.lastQuery());
    while(q.next())
    {
        counter++;
        fanfics.push_back(LoadFanfic(q));

        if(counter%10000 == 0)
            qDebug() << "tick " << counter/1000;
        //qDebug() << "tick " << counter;
    }
    qDebug() << "loaded fics:" << counter;


}

QSqlQuery MainWindow::BuildQuery()
{
    QSqlDatabase db = QSqlDatabase::database();
    auto query = queryBuilder.Build(filter);
    QSqlQuery q(db);
    q.prepare(query->str);
    auto it = query->bindings.begin();
    auto end = query->bindings.end();
    while(it != end)
    {
        qDebug() << it.key() << " " << it.value();
        q.bindValue(it.key(), it.value());
        ++it;
    }
    return q;
}



void MainWindow::ProcessTagsIntoGui()
{

    auto tagList = tagsInterface->ReadUserTags();
    QList<QPair<QString, QString>> tagPairs;

    for(auto tag : tagList)
        tagPairs.push_back({ "0", tag });
    ui->wdgTagsPlaceholder->InitFromTags(-1, tagPairs);
    FillRecTagBuildCombobox();
    FillRecTagCombobox();

}

void MainWindow::SetTag(int id, QString tag, bool silent)
{
    tagsInterface->SetTagForFic(id, tag);
    tagList = tagsInterface->ReadUserTags();
}

void MainWindow::UnsetTag(int id, QString tag)
{
    tagsInterface->RemoveTagFromFic(id, tag);
    tagList = tagsInterface->ReadUserTags();
}

QString MainWindow::CreateLimitQueryPart()
{
    QString result;
    int maxFicCountValue = ui->chkFicLimitActivated->isChecked() ? ui->sbMaxFicCount->value()  : 0;
    if(maxFicCountValue > 0 && maxFicCountValue < 51)
        result+= QString(" LIMIT %1 ").arg(QString::number(maxFicCountValue));
    return result;
}

PageTaskPtr MainWindow::CreatePageTaskFromUrls(QStringList urls, QString taskComment, int subTaskSize, int subTaskRetries,
                                               ECacheMode cacheMode, bool allowCacheRefresh)
{
    database::Transaction transaction(pageTaskInterface->db);

    auto timestamp = dbInterface->GetCurrentDateTime();
    qDebug() << timestamp;
    auto task = PageTask::CreateNewTask();
    task->allowedSubtaskRetries = subTaskRetries;
    task->cacheMode = cacheMode;
    task->parts = urls.size() / subTaskSize;
    task->refreshIfNeeded = allowCacheRefresh;
    task->taskComment = taskComment;
    task->type = 0;
    task->allowedRetries = 2;
    task->created = timestamp;
    task->isValid = true;
    task->scheduledTo = timestamp;
    task->startedAt = timestamp;
    pageTaskInterface->WriteTaskIntoDB(task);

    SubTaskPtr subtask;
    int i = 0;
    int counter = 0;
    do{
        auto last = i + subTaskSize <= urls.size() ? urls.begin() + i + subTaskSize : urls.end();
        subtask = PageSubTask::CreateNewSubTask();
        subtask->parent = task;
        auto content = SubTaskAuthorContent::NewContent();
        auto cast = static_cast<SubTaskAuthorContent*>(content.data());
        std::copy(urls.begin() + i, last, std::back_inserter(cast->authors));
        subtask->content = content;
        subtask->parentId = task->id;
        subtask->created = timestamp;
        subtask->size = last - (urls.begin() + i); // fucking idiot
        task->size += subtask->size;
        subtask->id = counter;
        subtask->isValid = true;
        subtask->allowedRetries = subTaskRetries;
        subtask->success = false;
        task->subTasks.push_back(subtask);
        i += subTaskSize;
        counter++;
    }while(i < urls.size());
    // now with subtasks
    pageTaskInterface->WriteTaskIntoDB(task);
    transaction.finalize();
    return task;
}

core::AuthorPtr CreateAuthorFromNameAndUrl(QString name, QString url)
{
    core::AuthorPtr author(new core::Author);
    author->name = name;
    author->SetWebID("ffn", url_utils::GetWebId(url, "ffn").toInt());
    return author;
}

void MainWindow::UseAuthorsPageTask(PageTaskPtr task,
                                    std::function<void(int)>callProgressCount,
                                    std::function<void(QString)>callProgressText)
{
    int currentCounter = 0;
    auto fanfics = fanficsInterface;
    auto authors = authorsInterface;
    pageTaskInterface->SetCurrentTask(task);
    auto job = [fanfics,authors](QString url, QString content){
        FavouriteStoryParser parser(fanfics);
        parser.ProcessPage(url, content);
        return parser;
    };

    QList<QFuture<FavouriteStoryParser>> futures;
    QList<FavouriteStoryParser> parsers;

    QSqlDatabase db = QSqlDatabase::database();
    QSqlDatabase tasksDb = QSqlDatabase::database("Tasks");

    int cachedPages = 0;
    int loadedPages = 0;
    bool breakerTriggered = false;
    StartPageWorker();
    for(auto subtask : task->subTasks)
    {
        qDebug() << "subtask test: " << subtask->id;
    }
    for(auto subtask : task->subTasks)
    {
        if(callProgressText)
            callProgressText(QString("<br>Starting new subtask: %1 <br>").arg(subtask->id));
        pageQueue.pending = true;
        qDebug() << "starting new subtask: " << subtask->id;
        // well this is bollocks :) wtf
        if(breakerTriggered)
            break;

        if(!subtask)
            continue;

        if(subtask->finished)
        {
            qDebug() << "subtask: " << subtask->id << " already processed its " << QString::number(subtask->content->size()) << "entities";
            qDebug() << "skipping";
            currentCounter+=subtask->content->size();
            if(callProgressCount)
                callProgressCount(currentCounter);
            continue;
        }
        if(subtask->attempted)
            qDebug() << "Retrying attempted task: " << task->id << " " << subtask->id;


        subtask->SetInitiated(dbInterface->GetCurrentDateTime());

        auto cast = static_cast<SubTaskAuthorContent*>(subtask->content.data());
        database::Transaction transaction(db);
        database::Transaction pcTransaction(pageTaskInterface->db);

        emit pageTaskList(cast->authors, subtask->parent.toStrongRef()->cacheMode);

        WebPage webPage;
        QSet<QString> fandoms;
        do
        {
            currentCounter++;
            futures.clear();
            parsers.clear();
            while(pageQueue.pending && pageQueue.data.size() == 0)
                QCoreApplication::processEvents();

            if(!pageQueue.pending && pageQueue.data.size() == 0)
                break;

            if(cancelCurrentTaskPressed)
            {
                cancelCurrentTaskPressed = false;
                breakerTriggered = true;
                break;
            }

            webPage = pageQueue.data[0];
            pageQueue.data.pop_front();

            if(!webPage.isFromCache)
                loadedPages++;
            else
                cachedPages++;

            qDebug() << "Page loaded in: " << webPage.loadedIn;
            callProgressCount(currentCounter);
            auto author = authorsInterface->GetById(url_utils::GetWebId(webPage.url, "ffn").toInt());
            //if author is not yet in the database, process his favourites and load him in
            if(!author)
            {
                auto startRecLoad = std::chrono::high_resolution_clock::now();
                auto splittings = SplitJob(webPage.content);

                //QString  authorName = splittings.authorName;
                if(splittings.favouriteStoryCountInWhole <= 2000)
                {
                    for(auto part: splittings.parts)
                    {
                        futures.push_back(QtConcurrent::run(job, webPage.url, part.data));
                    }
                    for(auto future: futures)
                    {
                        future.waitForFinished();
                        parsers+=future.result();
                    }
                }
                else if(callProgressText)
                    callProgressText("Skipping page with too much favourites<br>");

                auto elapsed = std::chrono::high_resolution_clock::now() - startRecLoad;
                qDebug() << "Page Processing done in: " << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                qDebug() << "Count of parts:" << parsers.size();

                FavouriteStoryParser sumParser;
                QString name = ParseAuthorNameFromFavouritePage(webPage.content);
                // need to create author when there is no data to parse

                auto author = CreateAuthorFromNameAndUrl(name, webPage.url);
                author->favCount = splittings.favouriteStoryCountInWhole;
                author->ficCount = splittings.authorStoryCountInWhole;
                auto webId = url_utils::GetWebId(webPage.url, "ffn").toInt();
                author->SetWebID("ffn", webId);
                sumParser.SetAuthor(author);

                authorsInterface->EnsureId(sumParser.recommender.author);
                author = authorsInterface->GetByWebID("ffn", webId);
                if(splittings.favouriteStoryCountInWhole == 0)
                {
                    qDebug() << "skipping author with no favourites";
                    callProgressText("skipping author with no favourites: " + webPage.url + "<br>");
                    continue;
                }
                for(auto actualParser: parsers)
                    sumParser.processedStuff+=actualParser.processedStuff;

                if(sumParser.processedStuff.size() < 2000)
                {
                    {
                        fanficsInterface->ProcessIntoDataQueues(sumParser.processedStuff);
                        fandoms.intersect(fandomsInterface->EnsureFandoms(sumParser.processedStuff));
                        QList<core::FicRecommendation> tempRecommendations;
                        tempRecommendations.reserve(sumParser.processedStuff.size());
                        for(auto& section : sumParser.processedStuff)
                            tempRecommendations.push_back({section, author});
                        fanficsInterface->AddRecommendations(tempRecommendations);
                        fanficsInterface->FlushDataQueues();
                        qDebug() << "skipped: " << fanficsInterface->skippedCounter;
                    }
                }
                if(callProgressText)
                {
                    callProgressText(webPage.url + " ");
                    callProgressText(" All Faves:  " + QString::number(sumParser.processedStuff.size()) + " " + "<br>");
                }

                elapsed = std::chrono::high_resolution_clock::now() - startRecLoad;
                qDebug() << "Completed author in: " << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            }
        }while(pageQueue.pending);
        subtask->SetFinished(dbInterface->GetCurrentDateTime());
        pageTaskInterface->WriteSubTaskIntoDB(subtask);
        fandomsInterface->RecalculateFandomStats(fandoms.values());
        fanficsInterface->ClearProcessedHash();
        transaction.finalize();
        pcTransaction.finalize();
        qDebug() << "subtask end";
    }
    StopPageWorker();
}


void MainWindow::LoadMoreAuthors()
{
    filter.mode = core::StoryFilter::filtering_in_recommendations;
    recsInterface->SetCurrentRecommendationList(recsInterface->GetListIdForName(ui->cbRecGroup->currentText()));
    QStringList authorUrls = recsInterface->GetLinkedPagesForList(recsInterface->GetCurrentRecommendationList());
    auto cacheMode = ui->chkWaveOnlyCache->isChecked() ? ECacheMode::use_only_cache : ECacheMode::dont_use_cache;
    QString comment = "Loading more authors from list: " + QString::number(recsInterface->GetCurrentRecommendationList());

    auto pageTask = CreatePageTaskFromUrls(authorUrls, comment, 20, 3, cacheMode, true);

    AddToProgressLog("Authors: " + QString::number(authorUrls.size()));
    ReinitProgressbar(authorUrls.size());


    DisableAllLoadButtons();
    UseAuthorsPageTask(pageTask, callProgress, callProgressText);

    //pcTransaction.finalize();

    //parser.ClearDoneCache();
    ui->edtResults->clear();
    //AddToProgressLog(" Pages read from cache: " + QString::number(cachedPages));
    //AddToProgressLog(" Pages read from web " + QString::number(loadedPages));
    AddToProgressLog(" Found recommenders: ");
    ShutdownProgressbar();
    ui->edtResults->setUpdatesEnabled(true);
    ui->edtResults->setReadOnly(true);
    EnableAllLoadButtons();
}

void MainWindow::UpdateAllAuthorsWith(std::function<void(QSharedPointer<core::Author>, WebPage)> updater)
{
    filter.mode = core::StoryFilter::filtering_in_recommendations;
    auto authors = authorsInterface->GetAllAuthors("ffn");
    AddToProgressLog("Authors: " + QString::number(authors.size()));

    ReinitProgressbar(authors.size());
    DisableAllLoadButtons();
    An<PageManager> pager;

    for(auto author: authors)
    {
        auto webPage = pager->GetPage(author->url("ffn"), ECacheMode::use_only_cache);
        qDebug() << "Page loaded in: " << webPage.loadedIn;
        pbMain->setValue(pbMain->value()+1);
        pbMain->setTextVisible(true);
        pbMain->setFormat("%v");
        updater(author, webPage);
    }
    //parser.ClearDoneCache();
    ui->edtResults->clear();
    ShutdownProgressbar();
    ui->edtResults->setUpdatesEnabled(true);
    ui->edtResults->setReadOnly(true);
    EnableAllLoadButtons();
}

void MainWindow::ReprocessAuthors()
{
    auto functor = [&](QSharedPointer<core::Author> author, WebPage webPage){
        //auto splittings = SplitJob(webPage.content);
        if(!author || author->id == -1)
            return;
        QString authorName = ParseAuthorNameFromFavouritePage(webPage.content);
        author->name = authorName;
        authorsInterface->AssignNewNameForAuthor(author, authorName);
    };
    UpdateAllAuthorsWith(functor);
}

void MainWindow::ProcessListIntoRecommendations(QString list)
{
    QFile data(list);
    QSqlDatabase db = QSqlDatabase::database();
    QStringList usedList;
    QList<int> usedIdList;
    if (data.open(QFile::ReadOnly))
    {
        QTextStream in(&data);
        QSharedPointer<core::RecommendationList> params(new core::RecommendationList);
        params->name = in.readLine().split("#").at(1);
        params->minimumMatch= in.readLine().split("#").at(1).toInt();
        params->pickRatio = in.readLine().split("#").at(1).toDouble();
        params->alwaysPickAt = in.readLine().split("#").at(1).toInt();
        recsInterface->LoadListIntoDatabase(params);
        database::Transaction transaction(db);
        QString str;
        do{
            str = in.readLine();
            QRegExp rx("/s/(\\d+)");
            int pos = rx.indexIn(str);
            QString ficIdPart;
            if(pos != -1)
            {
                ficIdPart = rx.cap(1);
            }
            if(ficIdPart.isEmpty())
                continue;
            //int id = database::GetFicDBIdByDelimitedSiteId(ficIdPart);

            if(ficIdPart.toInt() <= 0)
                continue;
            auto webId = ficIdPart.toInt();
            // at the moment works only for ffn and doesnt try to determine anything else

            auto id = fanficsInterface->GetIDFromWebID(webId, "ffn");
            if(id == -1)
                continue;
            qDebug()<< "Settign tag: " << "generictag" << " to: " << id;
            usedList.push_back(str);
            usedIdList.push_back(id);
            SetTag(id, "generictag");
        }while(!str.isEmpty());
        params->tagToUse ="generictag";
        BuildRecommendations(params);
        tagsInterface->DeleteTag("generictag");
        recsInterface->SetFicsAsListOrigin(usedIdList, params->id);
        transaction.finalize();
        qDebug() << "using list: " << usedList;
    }
}

void MainWindow::ProcessTagIntoRecommenders(QString listName)
{
    if(!recommendersModel)
        return;
    QStringList result;
    auto allStats = recsInterface->GetAuthorStatsForList(recsInterface->GetListIdForName(listName));
    for(auto stat : allStats)
        result.push_back(stat->authorName);
    recommendersModel->setStringList(result);
}



void MainWindow::DisableAllLoadButtons()
{
    ui->pbCrawl->setEnabled(false);
    ui->pbFirstWave->setEnabled(false);
    ui->pbLoadTrackedFandoms->setEnabled(false);
    ui->pbLoadPage->setEnabled(false);
    ui->pbLoadAllRecommenders->setEnabled(false);
}
void MainWindow::EnableAllLoadButtons()
{
    ui->pbCrawl->setEnabled(true);
    ui->pbFirstWave->setEnabled(true);
    ui->pbLoadTrackedFandoms->setEnabled(true);
    ui->pbLoadPage->setEnabled(true);
    ui->pbLoadAllRecommenders->setEnabled(true);
}


void MainWindow::OnNewPage(PageResult result)
{
    if(result.data.isValid)
        pageQueue.data.push_back(result.data);
    if(result.finished)
        pageQueue.pending = false;
}

void MainWindow::OnCopyFicUrl(QString text)
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(text);
    ui->edtResults->insertPlainText(text + "\n");

}

void MainWindow::OnOpenRecommenderLinks(QString url)
{
    if(!ui->chkHeartProfile->isChecked())
        return;
    auto webId = url_utils::GetWebId(url, "ffn");
    auto id = fanficsInterface->GetIDFromWebID(webId.toInt(), "ffn");
    auto recommenders = recsInterface->GetRecommendersForFicId(id);

    for(auto recommender : recommenders)
    {
        auto author = authorsInterface->GetById(recommender);
        if(!author || !recsInterface->GetMatchCountForRecommenderOnList(author->id, recsInterface->GetCurrentRecommendationList()))
            continue;
        QDesktopServices::openUrl(author->url("ffn"));
    }
}

void MainWindow::OnCopyAllUrls()
{
    QClipboard *clipboard = QApplication::clipboard();
    QString result;
    for(int i = 0; i < typetableModel->rowCount(); i ++)
    {
        if(ui->chkInfoForLinks->isChecked())
        {
            //result += typetableModel->index(i, 2).data().toString() + "\n";
        }
        result += "http://www.fanfiction.net/s/" + typetableModel->index(i, 9).data().toString() + "\n";//\n
    }
    clipboard->setText(result);
}

void MainWindow::OnDoFormattedList()
{
    QClipboard *clipboard = QApplication::clipboard();
    QString result;
    QList<int> ficIds;
    for(int i = 0; i < typetableModel->rowCount(); i ++)
        ficIds.push_back(typetableModel->index(i, 17).data().toInt());
    QSet<QPair<QString, int>> already;
    QMap<QString, QList<int>> byFandoms;
    for(auto id : ficIds)
    {
        auto ficPtr = fanficsInterface->GetFicById(id);

        auto fandoms = fanficsInterface->GetFandomsForFicAsNames(id);
        bool validGenre = true;
        //        for(auto genre : ficPtr->genres)
        //        {
        //            if(genre.trimmed().contains("Humor") || genre.trimmed().contains("Parody") || genre.trimmed().contains("not found"))
        //               validGenre = true;
        //        }
        //        qDebug() <<  validGenre  << " " << ficPtr->title;
        //        qDebug() <<  ficPtr->genres.join("/")  << " " << ficPtr->title;
        if(!validGenre)
            continue;
        if(fandoms.size() == 0)
        {
            auto fandom = ficPtr->fandom.trimmed();
            byFandoms[fandom].push_back(id);
            qDebug() << "no fandoms written for: " << "http://www.fanfiction.net/s/" + QString::number(ficPtr->webId) + ">";
        }
        for(auto fandom: fandoms)
        {
            byFandoms[fandom].push_back(id);
        }
    }

    result += "<ul>";
    for(auto fandomKey : byFandoms.keys())
        result+= "<li><a href=\"#" + fandomKey.toLower().replace(" ","_") +"\">" + fandomKey + "</a></li>";
    An<PageManager> pager;
    pager->SetDatabase(QSqlDatabase::database());
    for(auto fandomKey : byFandoms.keys())
    {
        result+="<h4 id=\""+ fandomKey.toLower().replace(" ","_") +  "\">" + fandomKey + "</h4>";

        for(auto fic : byFandoms[fandomKey])
        {
            QPair<QString, int> key = {fandomKey, fic};
            if(already.contains(key))
                continue;
            already.insert(key);
            auto ficPtr = fanficsInterface->GetFicById(fic);

            auto genreString = ficPtr->genreString;
            bool validGenre = true;
            //            for(auto genre : ficPtr->genres)
            //            {
            //                if(genre.trimmed().contains("Humor") || genre.trimmed().contains("Parody") || genre.trimmed().contains("not found"))
            //                   validGenre = true;
            //            }
            if(validGenre)
            {
                result+="<a href=http://www.fanfiction.net/s/" + QString::number(ficPtr->webId) + ">" + ficPtr->title + "</a> by " + ficPtr->author->name + "<br>";
                result+=ficPtr->genres.join("/")+ "<br><br>";
                QString status = "<b>Status:</b> <font color=\"%1\">%2</font>";

                if(ficPtr->complete)
                    result+=status.arg("green").arg("Complete<br>");
                else
                    result+=status.arg("red").arg("Active<br>");
                result+=ficPtr->summary + "<br><br>";
            }

        }
    }
    clipboard->setText(result);
}



void MainWindow::ReadSettings()
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    ui->chkShowDirectRecs->setVisible(settings.value("Settings/showExperimentaWaveparser", false).toBool());
    ui->pbReprocessAuthors->setVisible(settings.value("Settings/showListBuildButton", false).toBool());
    ui->wdgLower->setVisible(settings.value("Settings/showListBuildButton", false).toBool());
    ui->wdgWave->setVisible(settings.value("Settings/showExperimentaWaveparser", false).toBool());
    //ui->cbRecTagGroup->setVisible(settings.value("Settings/showExperimentaWaveparser", false).toBool());
    ui->pbFirstWave->setVisible(settings.value("Settings/showExperimentaWaveparser", false).toBool());
    ui->pbWipeFandom->setVisible(settings.value("Settings/pbWipeFandom", false).toBool());

    ui->chkTrackedFandom->setVisible(settings.value("Settings/showTracking", false).toBool());
    ui->pbLoadTrackedFandoms->setVisible(settings.value("Settings/showTracking", false).toBool());
    //pbLoadTrackedFandoms
    //chkActivateReloadSectionData
    ui->pbInit->setVisible(settings.value("Settings/showSectionReload", false).toBool());
    ui->chkActivateReloadSectionData->setVisible(settings.value("Settings/showSectionReload", false).toBool());

    ui->cbNormals->setCurrentText(settings.value("Settings/normals", "").toString());

    ui->chkTrackedFandom->blockSignals(true);
    auto fandomName = GetCurrentFandomName();
    auto fandom = fandomsInterface->GetFandom(fandomName);
    if(fandom)
        ui->chkTrackedFandom->setChecked(fandom->tracked);
    else
        ui->chkTrackedFandom->setChecked(false);
    ui->chkTrackedFandom->blockSignals(false);

    ui->cbMaxWordCount->setCurrentText(settings.value("Settings/maxWordCount", "").toString());
    ui->cbMinWordCount->setCurrentText(settings.value("Settings/minWordCount", 100000).toString());

    ui->leContainsGenre->setText(settings.value("Settings/plusGenre", "").toString());
    ui->leNotContainsGenre->setText(settings.value("Settings/minusGenre", "").toString());
    ui->leNotContainsWords->setText(settings.value("Settings/minusWords", "").toString());
    ui->leContainsWords->setText(settings.value("Settings/plusWords", "").toString());

    ui->chkHeartProfile->setChecked(settings.value("Settings/chkHeartProfile", false).toBool());
    ui->chkGenrePlus->setChecked(settings.value("Settings/chkGenrePlus", false).toBool());
    ui->chkGenreMinus->setChecked(settings.value("Settings/chkGenreMinus", false).toBool());
    ui->chkWordsPlus->setChecked(settings.value("Settings/chkWordsPlus", false).toBool());
    ui->chkWordsMinus->setChecked(settings.value("Settings/chkWordsMinus", false).toBool());

    ui->chkActive->setChecked(settings.value("Settings/active", false).toBool());
    ui->chkShowUnfinished->setChecked(settings.value("Settings/showUnfinished", false).toBool());
    ui->chkNoGenre->setChecked(settings.value("Settings/chkNoGenre", false).toBool());
    ui->chkCacheMode->setChecked(settings.value("Settings/cacheMode", false).toBool());
    ui->chkComplete->setChecked(settings.value("Settings/completed", false).toBool());
    ui->gbTagFilters->setChecked(settings.value("Settings/filterOnTags", false).toBool());
    ui->spMain->restoreState(settings.value("Settings/spMain", false).toByteArray());
    ui->spDebug->restoreState(settings.value("Settings/spDebug", false).toByteArray());
    ui->cbSortMode->blockSignals(true);
    ui->cbCustomFilters->blockSignals(true);
    ui->chkCustomFilter->blockSignals(true);
    ui->leAuthorUrl->setText(settings.value("Settings/currentRecommender", "").toString());
    //ui->chkShowRecsRegardlessOfTags->setChecked(settings.value("Settings/ignoreTagsOnRecommendations", false).toBool());
    ui->cbSortMode->setCurrentText(settings.value("Settings/currentSortFilter", "Update Date").toString());
    ui->cbCustomFilters->setCurrentText(settings.value("Settings/currentSortFilter", "Longest Running").toString());
    ui->cbWordCutoff->setCurrentText(settings.value("Settings/lengthCutoff", "100k Words").toString());
    ui->chkCustomFilter->setChecked(settings.value("Settings/customFilterEnabled", false).toBool());
    ui->cbSortMode->blockSignals(false);
    ui->cbCustomFilters->blockSignals(false);
    ui->chkCustomFilter->blockSignals(false);
    ui->cbBiasFavor->setCurrentText(settings.value("Settings/biasMode", "None").toString());
    ui->cbBiasOperator->setCurrentText(settings.value("Settings/biasOperator", "<").toString());
    ui->leBiasValue->setText(settings.value("Settings/biasValue", "2.5").toString());

}

void MainWindow::WriteSettings()
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    settings.setValue("Settings/minWordCount", ui->cbMinWordCount->currentText());
    settings.setValue("Settings/maxWordCount", ui->cbMaxWordCount->currentText());
    settings.setValue("Settings/normals", GetCurrentFandomName());
    //settings.setValue("Settings/crossovers", ui->cbCrossovers->currentText());
    settings.setValue("Settings/plusGenre", ui->leContainsGenre->text());
    settings.setValue("Settings/minusGenre", ui->leNotContainsGenre->text());
    settings.setValue("Settings/plusWords", ui->leContainsWords->text());
    settings.setValue("Settings/minusWords", ui->leNotContainsWords->text());


    settings.setValue("Settings/chkHeartProfile", ui->chkHeartProfile->isChecked());
    settings.setValue("Settings/chkGenrePlus", ui->chkGenrePlus->isChecked());
    settings.setValue("Settings/chkGenreMinus", ui->chkGenreMinus->isChecked());
    settings.setValue("Settings/chkWordsPlus", ui->chkWordsPlus->isChecked());
    settings.setValue("Settings/chkWordsMinus", ui->chkWordsMinus->isChecked());

    settings.setValue("Settings/active", ui->chkActive->isChecked());
    settings.setValue("Settings/showUnfinished", ui->chkShowUnfinished->isChecked());
    settings.setValue("Settings/chkNoGenre", ui->chkNoGenre->isChecked());
    settings.setValue("Settings/cacheMode", ui->chkCacheMode->isChecked());
    settings.setValue("Settings/completed", ui->chkComplete->isChecked());
    settings.setValue("Settings/filterOnTags", ui->gbTagFilters->isChecked());
    settings.setValue("Settings/spMain", ui->spMain->saveState());
    settings.setValue("Settings/spDebug", ui->spDebug->saveState());
    settings.setValue("Settings/currentSortFilter", ui->cbSortMode->currentText());
    settings.setValue("Settings/currentCustomFilter", ui->cbCustomFilters->currentText());
    settings.setValue("Settings/currentRecommender", ui->leAuthorUrl->text());
    //settings.setValue("Settings/ignoreTagsOnRecommendations", ui->chkShowRecsRegardlessOfTags->isChecked());
    settings.setValue("Settings/customFilterEnabled", ui->chkCustomFilter->isChecked());
    settings.setValue("Settings/biasMode", ui->cbBiasFavor->currentText());
    settings.setValue("Settings/biasOperator", ui->cbBiasOperator->currentText());
    settings.setValue("Settings/biasValue", ui->leBiasValue->text());
    settings.setValue("Settings/lengthCutoff", ui->cbWordCutoff->currentText());
    settings.setValue("Settings/currentList", ui->cbRecGroup->currentText());
    settings.sync();
}

QString MainWindow::GetCurrentFandomName()
{
    return core::Fandom::ConvertName(ui->cbNormals->currentText());
}

void MainWindow::OnChapterUpdated(QVariant id, QVariant chapter)
{
    fanficsInterface->AssignChapter(id.toInt(), chapter.toInt());

}


void MainWindow::OnTagAdd(QVariant tag, QVariant row)
{
    int rownum = row.toInt();
    SetTag(rownum, tag.toString());
    QModelIndex index = typetableModel->index(rownum, 10);
    auto data = typetableModel->data(index, 0).toString();
    data += " " + tag.toString();
    typetableModel->setData(index,data,0);
    typetableModel->updateAll();
}

void MainWindow::OnTagRemove(QVariant tag, QVariant row)
{
    UnsetTag(row.toInt(), tag.toString());
    QModelIndex index = typetableModel->index(row.toInt(), 10);
    auto data = typetableModel->data(index, 0).toString();
    data = data.replace(tag.toString(), "");
    typetableModel->setData(index,data,0);
    typetableModel->updateAll();
}

QString MainWindow::AppendCurrentSearchParameters(QString url)
{
    if(url.contains("/crossovers/"))
    {
        QStringList temp = url.split("/");
        url = "/" + temp.at(2) + "-Crossovers" + "/" + temp.at(3);
        url= url + "/0/";
    }
    QString lastPart = "/?&srt=1&lan=1&r=10&len=%1";
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    int lengthCutoff = ui->cbWordCutoff->currentText() == "100k Words" ? 100 : 60;
    lastPart=lastPart.arg(lengthCutoff);
    QString resultString = "https://www.fanfiction.net" + url + lastPart;





    qDebug() << resultString;
    return resultString;
}

void MainWindow::ReinitRecent(QString name)
{
    fandomsInterface->PushFandomToTopOfRecent(name);
    recentFandomsModel->setStringList(fandomsInterface->GetRecentFandoms());
}

void MainWindow::StartTaskTimer()
{
    taskTimer.setSingleShot(true);
    taskTimer.start(1000);
}

void MainWindow::CheckUnfinishedTasks()
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    if(settings.value("Settings/skipUnfinishedTasksCheck",true).toBool())
        return;
    auto tasks = pageTaskInterface->GetUnfinishedTasks();
    TaskList tasksToResume;
    for(auto task : tasks)
    {
        QString diagnostics;
        diagnostics+= "Unfinished task:\n";
        diagnostics+= task->taskComment + "\n";
        diagnostics+= "Started: " + task->startedAt.toString("yyyyMMdd hh:mm") + "\n";
        diagnostics+= "Do you want to continue this task?";
        QMessageBox m; /*(QMessageBox::Warning, "Unfinished task warning",
                      diagnostics,QMessageBox::Ok|QMessageBox::Cancel);*/
        m.setIcon(QMessageBox::Warning);
        m.setText(diagnostics);
        auto continueTask =  m.addButton("Continue",QMessageBox::AcceptRole);
        auto dropTask =      m.addButton("Drop task",QMessageBox::AcceptRole);
        auto delayDecision = m.addButton("Ask next time",QMessageBox::AcceptRole);
        Q_UNUSED(delayDecision);
        m.exec();
        if(m.clickedButton() == dropTask)
            pageTaskInterface->DropTaskId(task->id);
        else if(m.clickedButton() == continueTask)
            tasksToResume.push_back(task);
        else
        {
            //do nothing with this task. it will appear on next application start
        }
    }

    // later this needs to be preceded by code that routes tasks based on their type.
    // hard coding for now to make sure base functionality works
    {
        for(auto task : tasksToResume)
        {
            auto fullTask = pageTaskInterface->GetTaskById(task->id);
            InitUIFromTask(fullTask);
            UseAuthorsPageTask(fullTask, callProgress, callProgressText);
        }
    }
}


void MainWindow::on_pbCrawl_clicked()
{
    processedFics = 0;
    auto fandom = fandomsInterface->GetFandom(GetCurrentFandomName());
    if(!fandom)
        return;
    auto urls = fandom->GetUrls();
    //currentFilterUrls = GetCurrentFilterUrls(GetFandomName(), ui->rbCrossovers->isChecked(), true);

    pageCounter = 0;
    ui->edtResults->clear();
    processedCount = 0;
    ignoreUpdateDate = false;
    nextUrl = QString();
    //urls.pop_front();
    for(auto url: urls)
    {
        currentFilterUrl = url.GetUrl();
        auto lastUpdated = fandom->lastUpdateDate;
        RequestAndProcessPage(fandom->GetName(), lastUpdated, AppendCurrentSearchParameters(currentFilterUrl));
    }
    QMessageBox::information(nullptr, "Info", QString("finished processing %1 fics" ).arg(processedFics));
    ReinitRecent(fandom->GetName());
    ui->lvTrackedFandoms->setModel(recentFandomsModel);

}

void MainWindow::OnLinkClicked(const QUrl & url)
{
    if(url.toString().contains("fanfiction.net"))
        QDesktopServices::openUrl(url);
    else
        OpenTagWidget(QCursor::pos(), url.toString());
}


void MainWindow::OnTagToggled(int id, QString tag, bool checked)
{
    if(checked)
        SetTag(id, tag);
    else
        UnsetTag(id, tag);
}

void MainWindow::OnCustomFilterClicked()
{
    if(ui->chkCustomFilter->isChecked())
    {
        ui->cbCustomFilters->setEnabled(true);
        QPalette p = ui->cbCustomFilters->palette();
        ui->chkCustomFilter->setStyleSheet("QCheckBox {border: none; color: DarkGreen;}");
        ui->cbCustomFilters->setPalette(p);
    }
    else
    {
        ui->cbCustomFilters->setEnabled(false);
        QPalette p = ui->cbSortMode->palette();
        ui->cbCustomFilters->setPalette(p);
        ui->chkCustomFilter->setStyleSheet("");
    }
    //on_pbLoadDatabase_clicked();
}

void MainWindow::OnSectionReloadActivated()
{
    ui->pbInit->setEnabled(ui->chkActivateReloadSectionData->isChecked());
}


void MainWindow::OnShowContextMenu(QPoint p)
{
    browserMenu.popup(this->mapToGlobal(p));
}

void MainWindow::OnSectionChanged(QString)
{
    ui->cbNormals->setModel(new QStringListModel(fandomsInterface->GetFandomList()));
}

void MainWindow::on_pbLoadDatabase_clicked()
{
    filter = ProcessGUIIntoStoryFilter(core::StoryFilter::filtering_in_fics);
    LoadData();

    ui->edtResults->setUpdatesEnabled(true);
    ui->edtResults->setReadOnly(true);
    holder->SetData(fanfics);
    ReinitRecent(ui->cbNormals->currentText());
}



void MainWindow::OnCheckboxFilter(int)
{
    LoadData();
    ui->edtResults->setUpdatesEnabled(true);
    ui->edtResults->setReadOnly(true);
    holder->SetData(fanfics);
    typetableModel->OnReloadDataFromInterface();
}

void MainWindow::on_chkRandomizeSelection_clicked(bool checked)
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    auto ficLimitActive =  ui->chkFicLimitActivated->isChecked();
    int maxFicCountValue = ficLimitActive ? ui->sbMaxFicCount->value()  : 0;
    if(checked && (maxFicCountValue < 1 || maxFicCountValue >50))
        ui->sbMaxFicCount->setValue(settings.value("Settings/defaultRandomFicCount", 6).toInt());
}

void MainWindow::on_cbCustomFilters_currentTextChanged(const QString &)
{
    on_pbLoadDatabase_clicked();
}

void MainWindow::on_cbSortMode_currentTextChanged(const QString &)
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    if(ui->cbSortMode->currentText() == "Rec Count")
        ui->cbRecGroup->setVisible(settings.value("Settings/showExperimentaWaveparser", false).toBool());
}



void MainWindow::on_pbExpandPlusGenre_clicked()
{
    currentExpandedEdit = ui->leContainsGenre;
    CallExpandedWidget();
}

void MainWindow::on_pbExpandMinusGenre_clicked()
{
    currentExpandedEdit = ui->leNotContainsGenre;
    CallExpandedWidget();
}

void MainWindow::on_pbExpandPlusWords_clicked()
{
    currentExpandedEdit = ui->leContainsWords;
    CallExpandedWidget();
}

void MainWindow::on_pbExpandMinusWords_clicked()
{
    currentExpandedEdit = ui->leNotContainsWords;
    CallExpandedWidget();
}

void MainWindow::OnNewSelectionInRecentList(const QModelIndex &current, const QModelIndex &)
{
    ui->cbNormals->setCurrentText(current.data().toString());
    ui->chkTrackedFandom->blockSignals(true);
    auto fandom = fandomsInterface->GetFandom(GetCurrentFandomName());
    if(fandom)
        ui->chkTrackedFandom->setChecked(fandom->tracked);

    ui->chkTrackedFandom->blockSignals(false);
}

void MainWindow::OnNewSelectionInRecommenderList(const QModelIndex &current, const QModelIndex &)
{
    QString recommender = current.data().toString();
    auto author = authorsInterface->GetAuthorByNameAndWebsite(recommender, "ffn");
    if(author)
    {
        ui->leAuthorUrl->setText(author->url("ffn"));
        ui->cbAuthorNames->setCurrentText(author->name);
    }

}

void MainWindow::CallExpandedWidget()
{
    if(!expanderWidget)
    {
        expanderWidget = new QDialog();
        expanderWidget->resize(400, 300);
        QVBoxLayout* vl = new QVBoxLayout;
        QPushButton* okButton = new QPushButton;
        okButton->setText("OK");
        edtExpander = new QTextEdit;
        vl->addWidget(edtExpander);
        vl->addWidget(okButton);
        expanderWidget->setLayout(vl);
        connect(okButton, &QPushButton::clicked, [&](){
            if(currentExpandedEdit)
                currentExpandedEdit->setText(edtExpander->toPlainText());
            expanderWidget->hide();
        });
    }
    if(currentExpandedEdit)
        edtExpander->setText(currentExpandedEdit->text());
    expanderWidget->exec();
}

QStringList MainWindow::SortedList(QStringList list)
{
    qSort(list.begin(),list.end());
    return list;
}
QList<QSharedPointer<core::Author> >  MainWindow::ReverseSortedList(QList<QSharedPointer<core::Author> >  list)
{
    qSort(list.begin(),list.end(), [](QSharedPointer<core::Author> a1, QSharedPointer<core::Author> a2){
        return a1->name < a2->name;
    });
    std::reverse(list.begin(),list.end());
    return list;
}


QStringList MainWindow::GetUniqueAuthorsFromActiveRecommenderSet()
{
    QStringList result;
    QList<QSharedPointer<core::Fic>> sections;
    int counter = 0;
    auto list = ReverseSortedList(recsInterface->GetAuthorsForRecommendationList(recsInterface->GetCurrentRecommendationList()));
    auto fanfics = fanficsInterface;
    auto authors = authorsInterface;
    auto job = [fanfics,authors](QString url, QString content){
        QList<QSharedPointer<core::Fic> > sections;
        FavouriteStoryParser parser(fanfics);
        sections += parser.ProcessPage(url, content);
        return sections;
    };
    QList<QFuture<QList<QSharedPointer<core::Fic>>>> futures;
    An<PageManager> pager;
    QHash<QString, QSharedPointer<core::Fic>> uniqueSections;
    pager->SetDatabase(QSqlDatabase::database()); //! todo needs updating for postgre
    for(auto author: list)
    {

        auto startRecProcessing = std::chrono::high_resolution_clock::now();
        counter++;
        uniqueSections[author->url("ffn")] = QSharedPointer<core::Fic>(new core::Fic);

        InsertLogIntoEditor(ui->edtResults, author->url("ffn"));
        auto page = pager->GetPage(author->url("ffn"), ECacheMode::use_cache);
        if(!page.isFromCache)
            pager->SavePageToDB(page);


        auto splittings = SplitJob(page.content);
        for(auto part: splittings.parts)
        {
            futures.push_back(QtConcurrent::run(job, page.url, part.data));
        }
        for(auto future: futures)
        {
            future.waitForFinished();
            sections+=future.result();
        }
        if(!page.isFromCache)
        {
            qDebug() << "Sleeping";
            QThread::sleep(1);
        }
        auto elapsed = std::chrono::high_resolution_clock::now() - startRecProcessing;
        qDebug() << "Recommender processing done in: " << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    }


    for(auto section: sections)
        uniqueSections[section->author->url("ffn")] = section;
    result = SortedList(uniqueSections.keys());
    return result;
}


void MainWindow::CreatePageThreadWorker()
{
    worker = new PageThreadWorker;
    worker->moveToThread(&pageThread);
    connect(this, &MainWindow::pageTask, worker, &PageThreadWorker::Task);
    connect(this, &MainWindow::pageTaskList, worker, &PageThreadWorker::TaskList);
    connect(worker, &PageThreadWorker::pageResult, this, &MainWindow::OnNewPage);
}

void MainWindow::StartPageWorker()
{
    pageQueue.data.clear();
    pageQueue.pending = true;
    pageThread.start(QThread::HighPriority);

}

void MainWindow::StopPageWorker()
{
    pageThread.quit();
}

void MainWindow::ReinitProgressbar(int maxValue)
{
    pbMain->setMaximum(maxValue);
    pbMain->setValue(0);
    pbMain->show();
}

void MainWindow::ShutdownProgressbar()
{
    pbMain->setValue(0);
    pbMain->hide();
}

void MainWindow::AddToProgressLog(QString value)
{
    ui->edtResults->insertHtml(value);
}

void MainWindow::FillRecTagBuildCombobox()
{
    ui->cbRecTagBuildGroup->setModel(new QStringListModel(tagList));

}

void MainWindow::FillRecTagCombobox()
{
    auto lists = recsInterface->GetAllRecommendationListNames();
    ui->cbRecGroup->setModel(new QStringListModel(lists));
    ui->cbAuthorNames->setModel(new QStringListModel(lists));
    //ui->cbRecTagGroup->setModel(new QStringListModel(lists));
}

void MainWindow::FillRecommenderListView(bool forceRefresh)
{
    QStringList result;
    auto list = recsInterface->GetCurrentRecommendationList();
    auto allStats = recsInterface->GetAuthorStatsForList(list, forceRefresh);
    std::sort(std::begin(allStats),std::end(allStats), [](auto s1, auto s2){
        return s1->matchRatio < s2->matchRatio;
    });
    for(auto stat : allStats)
        result.push_back(stat->authorName);
    recommendersModel->setStringList(result);
    ui->lvRecommenders->setModel(recommendersModel);
    ui->cbAuthorNames->setModel(recommendersModel);
}

bool MainWindow::LoadAuthor(QString url)
{
    auto startPageRequest = std::chrono::high_resolution_clock::now();
    auto page = RequestPage(url.trimmed(),  ECacheMode::dont_use_cache);
    auto elapsed = std::chrono::high_resolution_clock::now() - startPageRequest;
    qDebug() << "Fetched page in: " << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    FavouriteStoryParser parser(fanficsInterface);
    auto startPageProcess = std::chrono::high_resolution_clock::now();
    QString name = ParseAuthorNameFromFavouritePage(page.content);
    parser.authorName = name;
    parser.ProcessPage(page.url, page.content);
    elapsed = std::chrono::high_resolution_clock::now() - startPageProcess;
    qDebug() << "Processed page in: " << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    ui->edtResults->clear();
    ui->edtResults->insertHtml(parser.diagnostics.join(""));
    QSqlDatabase db = QSqlDatabase::database();
    database::Transaction transaction(db);
    QSet<QString> fandoms;
    authorsInterface->EnsureId(parser.recommender.author); // assuming ffn
    auto author = authorsInterface->GetByWebID("ffn", url_utils::GetWebId(ui->leAuthorUrl->text(), "ffn").toInt());
    {
        fanficsInterface->ProcessIntoDataQueues(parser.processedStuff);
        fandoms = fandomsInterface->EnsureFandoms(parser.processedStuff);
        QList<core::FicRecommendation> recommendations;
        recommendations.reserve(parser.processedStuff.size());
        for(auto& section : parser.processedStuff)
            recommendations.push_back({section, author});
        fanficsInterface->AddRecommendations(recommendations);
        fanficsInterface->FlushDataQueues();

        fandomsInterface->RecalculateFandomStats(fandoms.values());
    }
    transaction.finalize();
    return true;
}


void MainWindow::on_chkTrackedFandom_toggled(bool checked)
{
    fandomsInterface->SetTracked(GetCurrentFandomName(),checked);
}



void MainWindow::on_pbLoadTrackedFandoms_clicked()
{
    processedFics = 0;
    auto fandoms = fandomsInterface->ListOfTrackedFandoms();
    qDebug() << fandoms;
    for(auto fandom : fandoms)
    {
        auto urls = fandom->GetUrls();
        pageCounter = 0;
        ui->edtResults->clear();
        processedCount = 0;
        ignoreUpdateDate = false;
        nextUrl = QString();
        for(auto url: urls)
            RequestAndProcessPage(fandom->GetName(), lastUpdated.date(), url.GetUrl());
    }
    QMessageBox::information(nullptr, "Info", QString("finished processing %1 fics" ).arg(processedFics));
}

void MainWindow::on_pbLoadPage_clicked()
{
    //! todo rec list needs source fics saved
    filter = ProcessGUIIntoStoryFilter(core::StoryFilter::filtering_in_recommendations, true);
    //ui->leAuthorUrl->text()
    LoadAuthor(ui->leAuthorUrl->text());
    LoadData();
    ui->edtResults->setUpdatesEnabled(true);
    ui->edtResults->setReadOnly(true);
    holder->SetData(fanfics);
}


void MainWindow::on_pbOpenRecommendations_clicked()
{
    filter = ProcessGUIIntoStoryFilter(core::StoryFilter::filtering_in_recommendations, true);

    auto startRecLoad = std::chrono::high_resolution_clock::now();
    LoadData();
    auto elapsed = std::chrono::high_resolution_clock::now() - startRecLoad;

    qDebug() << "Loaded recs in: " << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    ui->edtResults->setUpdatesEnabled(true);
    ui->edtResults->setReadOnly(true);
    holder->SetData(fanfics);
}

void MainWindow::on_pbLoadAllRecommenders_clicked()
{
    filter = ProcessGUIIntoStoryFilter(core::StoryFilter::filtering_in_recommendations);
    QSqlDatabase db = QSqlDatabase::database();
    database::Transaction transaction(db);

    recsInterface->SetCurrentRecommendationList(recsInterface->GetListIdForName(ui->cbRecGroup->currentText()));
    auto recList = recsInterface->GetCurrentRecommendationList();
    auto authors = recsInterface->GetAuthorsForRecommendationList(recList);
    pbMain->setMaximum(authors.size());
    pbMain->show();
    pbMain->setValue(0);
    pbMain->setTextVisible(true);
    pbMain->setFormat("%v");

    QSet<QString> fandoms;
    QList<core::FicRecommendation> recommendations;
    auto fanficsInterface = this->fanficsInterface;
    auto authorsInterface = this->authorsInterface;
    auto job = [fanficsInterface,authorsInterface](QString url, QString content){
        QList<QSharedPointer<core::Fic> > sections;
        FavouriteStoryParser parser(fanficsInterface);
        sections += parser.ProcessPage(url, content);
        return sections;
    };

    for(auto author: authors)
    {
        QList<QSharedPointer<core::Fic>> sections;
        QList<QFuture<QList<QSharedPointer<core::Fic>>>> futures;
        QSet<QString> uniqueAuthors;
        authorsInterface->DeleteLinkedAuthorsForAuthor(author->id);
        auto startPageRequest = std::chrono::high_resolution_clock::now();
        auto page = RequestPage(author->url("ffn"), ui->chkWaveOnlyCache->isChecked() ? ECacheMode::use_cache : ECacheMode::dont_use_cache);
        auto elapsed = std::chrono::high_resolution_clock::now() - startPageRequest;
        qDebug() << "Fetched page in: " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        auto startPageProcess = std::chrono::high_resolution_clock::now();
        FavouriteStoryParser parser(fanficsInterface);
        //parser.ProcessPage(page.url, page.content);

        auto splittings = SplitJob(page.content);
        for(auto part: splittings.parts)
        {
            futures.push_back(QtConcurrent::run(job, page.url, part.data));
        }
        for(auto future: futures)
        {
            future.waitForFinished();
        }
        for(auto future: futures)
            parser.processedStuff += future.result();

        {
            fanficsInterface->ProcessIntoDataQueues(parser.processedStuff);
            auto fandoms = fandomsInterface->EnsureFandoms(parser.processedStuff);
            fandoms.intersect(fandoms);
            QList<core::FicRecommendation> tempRecommendations;
            tempRecommendations.reserve(parser.processedStuff.size());
            uniqueAuthors.reserve(parser.processedStuff.size());
            for(auto& section : parser.processedStuff)
            {
                tempRecommendations.push_back({section, author});
                if(!uniqueAuthors.contains(section->author->url("ffn")))
                    uniqueAuthors.insert(section->author->url("ffn"));
            }
            fanficsInterface->AddRecommendations(tempRecommendations);
            fanficsInterface->FlushDataQueues();
            authorsInterface->UploadLinkedAuthorsForAuthor(author->id, uniqueAuthors.values());
            qDebug() << "skipped: " << fanficsInterface->skippedCounter;
        }

        ui->edtResults->clear();
        ui->edtResults->insertHtml(parser.diagnostics.join(""));
        pbMain->setValue(pbMain->value()+1);
        pbMain->setTextVisible(true);
        pbMain->setFormat("%v");
        QCoreApplication::processEvents();

        elapsed = std::chrono::high_resolution_clock::now() - startPageProcess;
        qDebug() << "Processed page in: " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    }
    fandomsInterface->RecalculateFandomStats(fandoms.values());
    transaction.finalize();
    fanficsInterface->ClearProcessedHash();
    pbMain->hide();
    ui->leAuthorUrl->setText("");
    auto startRecLoad = std::chrono::high_resolution_clock::now();
    LoadData();
    auto elapsed = std::chrono::high_resolution_clock::now() - startRecLoad;
    qDebug() << "Loaded recs in: " << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    ui->edtResults->setUpdatesEnabled(true);
    ui->edtResults->setReadOnly(true);
    holder->SetData(fanfics);
}

void MainWindow::on_pbOpenWholeList_clicked()
{
    filter = ProcessGUIIntoStoryFilter(core::StoryFilter::filtering_in_recommendations);

    ui->leAuthorUrl->setText("");
    auto startRecLoad = std::chrono::high_resolution_clock::now();
    LoadData();
    auto elapsed = std::chrono::high_resolution_clock::now() - startRecLoad;
    qDebug() << "Loaded recs in: " << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    ui->edtResults->setUpdatesEnabled(true);
    ui->edtResults->setReadOnly(true);
    holder->SetData(fanfics);
}

void MainWindow::on_pbFirstWave_clicked()
{
    LoadMoreAuthors();
}

void MainWindow::OnReloadRecLists()
{
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("UTF-8"));
    auto currentList = settings.value("Settings/recommendationListName").toString();
    QStringList recommenderList;
    if(!currentList.isEmpty())
        recommenderList = recsInterface->GetNamesForListId(recsInterface->GetListIdForName(currentList));
    recommendersModel->setStringList(SortedList(recommenderList));
}

void MainWindow::on_cbUseDateCutoff_clicked()
{
    ui->deCutoffLimit->setEnabled(!ui->deCutoffLimit->isEnabled());
}


int MainWindow::BuildRecommendations(QSharedPointer<core::RecommendationList> params)
{
    QSqlDatabase db = QSqlDatabase::database();
    database::Transaction transaction(db);

    authorsInterface->Clear();
    authorsInterface->LoadAuthors("ffn");
    recsInterface->Clear();
    //fanficsInterface->ClearIndex()
    QList<int> allAuthors = authorsInterface->GetAllAuthorIds();;
    std::sort(std::begin(allAuthors),std::end(allAuthors));
    qDebug() << "count of author ids: " << allAuthors.size();
    QList<int> filteredAuthors;
    filteredAuthors.reserve(allAuthors.size()/10);
    auto listId = recsInterface->GetListIdForName(params->name);
    recsInterface->DeleteList(listId);
    recsInterface->LoadListIntoDatabase(params);
    int counter = 0;
    int alLCounter = 0;
    for(auto authorId: allAuthors)
    {
        auto stats = authorsInterface->GetStatsForTag(authorId, params);


        if( stats->matchesWithReference >= params->alwaysPickAt
                || (stats->matchRatio <= params->pickRatio && stats->matchesWithReference >= params->minimumMatch) )
        {
            alLCounter++;
            auto author = authorsInterface->GetById(authorId);
            if(author)
                qDebug() << "Fit for criteria: " << author->name;
            recsInterface->LoadAuthorRecommendationsIntoList(authorId, params->id);
            recsInterface->LoadAuthorRecommendationStatsIntoDatabase(params->id, stats);
            recsInterface->IncrementAllValuesInListMatchingAuthorFavourites(authorId,params->id);
            filteredAuthors.push_back(authorId);
            counter++;
        }
    }

    recsInterface->UpdateFicCountInDatabase(params->id);
    recsInterface->SetCurrentRecommendationList(params->id);
    if(filteredAuthors.size() > 0)
    {
        FillRecTagCombobox();
        FillRecommenderListView();
    }

    //recsInterface->GetAuthorStatsForList()
    //recommenders = database::FetchRecommenders();
    //recommendersModel->setStringList(SortedList(recommenders.keys()));
    //ui->lvRecommenders->setModel(recommendersModel);
    transaction.finalize();
    qDebug() << "processed authors: " << counter;
    qDebug() << "all authors: " << alLCounter;
    return params->id;
}

core::StoryFilter MainWindow::ProcessGUIIntoStoryFilter(core::StoryFilter::EFilterMode mode, bool useAuthorLink)
{
    auto valueIfChecked = [](QCheckBox* box, auto value){
        if(box->isChecked())
            return value;
        return decltype(value)();
    };
    core::StoryFilter filter;
    filter.activeTags = ui->wdgTagsPlaceholder->GetSelectedTags();
    qDebug() << filter.activeTags;
    filter.allowNoGenre = ui->chkNoGenre->isChecked();
    filter.allowUnfinished = ui->chkShowUnfinished->isChecked();
    filter.ensureActive = ui->chkActive->isChecked();
    filter.ensureCompleted= ui->chkComplete->isChecked();
    filter.fandom = GetCurrentFandomName();

    filter.genreExclusion = valueIfChecked(ui->chkGenreMinus, core::StoryFilter::ProcessDelimited(ui->leNotContainsGenre->text(), "###"));
    filter.genreInclusion = valueIfChecked(ui->chkGenrePlus,core::StoryFilter::ProcessDelimited(ui->leContainsGenre->text(), "###"));
    filter.wordExclusion = valueIfChecked(ui->chkWordsMinus, core::StoryFilter::ProcessDelimited(ui->leNotContainsWords->text(), "###"));
    filter.wordInclusion = valueIfChecked(ui->chkWordsPlus, core::StoryFilter::ProcessDelimited(ui->leContainsWords->text(), "###"));
    filter.ignoreAlreadyTagged = ui->chkIgnoreTags->isChecked();
    filter.includeCrossovers =false; //ui->rbCrossovers->isChecked();
    filter.maxFics = valueIfChecked(ui->chkFicLimitActivated, ui->sbMaxFicCount->value());
    filter.minFavourites = valueIfChecked(ui->chkFaveLimitActivated, ui->sbMinimumFavourites->value());
    filter.maxWords= ui->cbMaxWordCount->currentText().toInt();
    filter.minWords= ui->cbMinWordCount->currentText().toInt();
    filter.randomizeResults = ui->chkRandomizeSelection->isChecked();
    filter.recentAndPopularFavRatio = ui->sbFavrateValue->value();
    filter.recentCutoff = ui->dteFavRateCut->dateTime();
    filter.reviewBias = static_cast<core::StoryFilter::EReviewBiasMode>(ui->cbBiasFavor->currentIndex());
    filter.biasOperator = static_cast<core::StoryFilter::EBiasOperator>(ui->cbBiasOperator->currentIndex());
    filter.reviewBiasRatio = ui->leBiasValue->text().toDouble();
    filter.sortMode = static_cast<core::StoryFilter::ESortMode>(ui->cbSortMode->currentIndex());
    filter.showOriginsInLists = ui->chkShowOrigins->isChecked();
    filter.minRecommendations = ui->sbMinRecommendations->value();
    //if(ui->cbSortMode->currentText())
    filter.listForRecommendations = recsInterface->GetListIdForName(ui->cbRecGroup->currentText());
    //filter.titleInclusion = nothing for now
    filter.website = "ffn"; // just ffn for now
    filter.mode = mode;
    QString authorUrl = ui->leAuthorUrl->text();
    auto author = authorsInterface->GetByWebID("ffn", url_utils::GetWebId(authorUrl, "ffn").toInt());
    if(author && useAuthorLink)
        filter.useThisRecommenderOnly = author->id;
    return filter;
}

QSharedPointer<core::RecommendationList> MainWindow::BuildRecommendationParamsFromGUI()
{
    QSharedPointer<core::RecommendationList> params(new core::RecommendationList);
    params->name = ui->cbRecListNames->currentText();
    params->tagToUse = ui->cbRecTagBuildGroup->currentText();
    params->minimumMatch = ui->sbMinRecMatch->value();
    params->pickRatio = ui->dsbMinRecThreshhold->value();
    params->alwaysPickAt = ui->sbAlwaysPickRecAt->value();
    return params;
}

void MainWindow::on_pbBuildRecs_clicked()
{
    BuildRecommendations(BuildRecommendationParamsFromGUI());
    //?  do I need to full reload here?
    recsInterface->LoadAvailableRecommendationLists();
}

//void MainWindow::on_cbRecTagGroup_currentIndexChanged(const QString &tag)
//{
//    //ProcessTagIntoRecommenders(tag);
//    recsInterface->SetCurrentRecommendationList(recsInterface->GetListIdForName(ui->cbRecTagGroup->currentText()));
//    FillRecommederListView();
//}

void MainWindow::on_pbOpenAuthorUrl_clicked()
{
    QDesktopServices::openUrl(ui->leAuthorUrl->text());
}

void MainWindow::on_pbReprocessAuthors_clicked()
{
    //ReprocessTagSumRecs();
    ProcessListIntoRecommendations("lists/source.txt");
}

void MainWindow::on_cbRecTagBuildGroup_currentTextChanged(const QString &newText)
{
    auto list = recsInterface->GetList(newText);
    if(list)
    {
        ui->sbMinRecMatch->setValue(list->minimumMatch);
        ui->dsbMinRecThreshhold->setValue(list->pickRatio);
        ui->sbAlwaysPickRecAt->setValue(list->alwaysPickAt);
    }
    else
    {
        ui->sbMinRecMatch->setValue(1);
        ui->dsbMinRecThreshhold->setValue(150);
        ui->sbAlwaysPickRecAt->setValue(1);
        ui->cbRecListNames->setCurrentText("lupine");
    }
}

void MainWindow::OnCopyFavUrls()
{
    QClipboard *clipboard = QApplication::clipboard();
    QString result;
    for(int i = 0; i < recommendersModel->rowCount(); i ++)
    {
        auto author = authorsInterface->GetAuthorByNameAndWebsite(recommendersModel->index(i, 0).data().toString(), "ffn");
        if(!author)
            continue;
        result += author->url("ffn") + "\n";
    }
    clipboard->setText(result);
}

void MainWindow::on_cbRecGroup_currentIndexChanged(const QString &arg1)
{
    recsInterface->SetCurrentRecommendationList(recsInterface->GetListIdForName(ui->cbRecGroup->currentText()));
    if(ui->chkSyncListNameToView->isChecked())
        ui->leCurrentListName->setText(ui->cbRecGroup->currentText());
    FillRecommenderListView();
}

void MainWindow::on_pbCreateNewList_clicked()
{
    QSharedPointer<core::RecommendationList> params;
    auto listName = ui->leCurrentListName->text().trimmed();
    auto listId = recsInterface->GetListIdForName(listName);
    if(listId != -1)
    {
        QMessageBox::warning(nullptr, "Warning!", "Can't create a list with a name that already exists, choose another name");
        return;
    }
    params->name = listName;
    recsInterface->LoadListIntoDatabase(params);
    FillRecTagCombobox();

}

void MainWindow::on_pbRemoveList_clicked()
{
    auto listName = ui->leCurrentListName->text().trimmed();
    auto listId = recsInterface->GetListIdForName(listName);
    if(listId == -1)
        return;
    auto button = QMessageBox::question(nullptr, "Question", "Do you really want to delete the recommendation list?");
    if(button == QMessageBox::No)
        return;
    recsInterface->DeleteList(listId);
    FillRecTagCombobox();
}

void MainWindow::on_pbAddAuthorToList_clicked()
{
    QString url = ui->leAuthorUrl->text().trimmed();
    QSqlDatabase db = QSqlDatabase::database();
    database::Transaction transaction(db);
    if(url.isEmpty())
        return;
    if(!LoadAuthor(url))
        return;
    QString listName = ui->leCurrentListName->text().trimmed();
    if(listName.isEmpty())
    {
        QMessageBox::warning(nullptr, "Warning!", "You need at least some name to create a new list. Please enter the list name.");
    }


    auto listId = recsInterface->GetListIdForName(ui->leCurrentListName->text().trimmed());
    if(listId == -1)
    {
        QSharedPointer<core::RecommendationList> params(new core::RecommendationList);
        params->name = listName;
        recsInterface->LoadListIntoDatabase(params);
        listId = recsInterface->GetListIdForName(ui->leCurrentListName->text().trimmed());
    }
    auto author = authorsInterface->GetByWebID("ffn", url_utils::GetWebId(url, "ffn").toInt());
    if(!author)
        return;
    QSharedPointer<core::AuthorRecommendationStats> stats(new core::AuthorRecommendationStats);
    stats->authorId = author->id;
    stats->listId = listId;
    stats->authorName = author->name;
    recsInterface->LoadAuthorRecommendationsIntoList(author->id, listId);
    recsInterface->LoadAuthorRecommendationStatsIntoDatabase(listId, stats);
    recsInterface->IncrementAllValuesInListMatchingAuthorFavourites(author->id,listId);
    transaction.finalize();
    auto lists = recsInterface->GetAllRecommendationListNames();
    ui->cbRecGroup->setModel(new QStringListModel(lists));
    if(ui->cbRecGroup->currentText()== ui->leCurrentListName->text().trimmed())
        FillRecommenderListView(true);
}

void MainWindow::on_pbRemoveAuthorFromList_clicked()
{
    QString url = ui->leAuthorUrl->text().trimmed();
    QSqlDatabase db = QSqlDatabase::database();
    database::Transaction transaction(db);
    if(url.isEmpty())
        return;
    auto author = authorsInterface->GetByWebID("ffn", url_utils::GetWebId(url, "ffn").toInt());
    if(!author)
        return;

    QString listName = ui->leCurrentListName->text().trimmed();
    auto listId = recsInterface->GetListIdForName(listName);
    if(listId == -1)
        return;

    recsInterface->RemoveAuthorRecommendationStatsFromDatabase(listId, author->id);
    recsInterface->DecrementAllValuesInListMatchingAuthorFavourites(author->id,listId);
    transaction.finalize();
    if(ui->cbRecGroup->currentText()== ui->leCurrentListName->text().trimmed())
        FillRecommenderListView(true);
}

void MainWindow::OnCheckUnfinishedTasks()
{
    CheckUnfinishedTasks();
}
