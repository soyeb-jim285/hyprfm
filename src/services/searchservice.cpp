#include "services/searchservice.h"
#include "models/searchresultsmodel.h"
#include <QStandardPaths>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

// ── SearchWorker (runs in separate thread) ──

SearchWorker::SearchWorker(QObject *parent) : QObject(parent) {}

void SearchWorker::search(const QString &rootPath, const QString &pattern, bool showHidden, int maxResults)
{
    m_cancelled = false;
    int count = 0;

    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
    if (showHidden) filters |= QDir::Hidden;

    QDirIterator it(rootPath, filters, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);

    bool isGlob = pattern.contains('*') || pattern.contains('?') || pattern.contains('[');
    QRegularExpression regex;
    if (isGlob) {
        QString re = QRegularExpression::wildcardToRegularExpression(pattern);
        regex = QRegularExpression(re, QRegularExpression::CaseInsensitiveOption);
    }

    QList<QFileInfo> batch;
    while (it.hasNext() && !m_cancelled && count < maxResults) {
        it.next();
        QFileInfo info = it.fileInfo();
        QString name = info.fileName();

        bool matches = isGlob ? regex.match(name).hasMatch()
                              : name.contains(pattern, Qt::CaseInsensitive);
        if (!matches) continue;

        batch.append(info);
        count++;

        if (batch.size() >= 50) {
            emit resultsReady(batch);
            batch.clear();
        }
    }
    if (!batch.isEmpty())
        emit resultsReady(batch);

    emit finished();
}

void SearchWorker::cancel()
{
    m_cancelled = true;
}

// ── SearchService ──

SearchService::SearchService(QObject *parent)
    : QObject(parent)
{
    m_fdPath = QStandardPaths::findExecutable("fd");
}

SearchService::~SearchService()
{
    cancelSearch();
}

void SearchService::setResultsModel(SearchResultsModel *model)
{
    m_model = model;
}

bool SearchService::hasFd() const { return !m_fdPath.isEmpty(); }
bool SearchService::isSearching() const { return m_searching; }
int SearchService::resultCount() const { return m_resultCount; }
int SearchService::maxResults() const { return m_maxResults; }

void SearchService::setMaxResults(int max)
{
    if (m_maxResults == max) return;
    m_maxResults = max;
    emit maxResultsChanged();
}

void SearchService::startSearch(const QString &rootPath, const QString &pattern, bool showHidden)
{
    cancelSearch();

    if (!m_model) return;
    m_model->clear();
    m_resultCount = 0;
    emit resultCountChanged();

    m_searching = true;
    emit isSearchingChanged();

    if (hasFd())
        startFdSearch(rootPath, pattern, showHidden);
    else
        startIteratorSearch(rootPath, pattern, showHidden);
}

void SearchService::cancelSearch()
{
    if (m_fdProcess) {
        m_fdProcess->kill();
        m_fdProcess->waitForFinished(1000);
        delete m_fdProcess;
        m_fdProcess = nullptr;
    }
    if (m_worker) {
        m_worker->cancel();
    }
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(2000);
        delete m_workerThread;
        m_workerThread = nullptr;
        m_worker = nullptr;
    }
    if (m_searching) {
        m_searching = false;
        emit isSearchingChanged();
    }
}

void SearchService::startFdSearch(const QString &rootPath, const QString &pattern, bool showHidden)
{
    m_fdProcess = new QProcess(this);

    QStringList args;
    args << "--color" << "never" << "--follow";
    if (showHidden) args << "--hidden";
    args << "--max-results" << QString::number(m_maxResults);

    bool isGlob = pattern.contains('*') || pattern.contains('?') || pattern.contains('[');
    if (isGlob) {
        args << "--glob" << pattern;
    } else {
        args << pattern;
    }
    args << rootPath;

    connect(m_fdProcess, &QProcess::readyReadStandardOutput, this, &SearchService::handleFdOutput);
    connect(m_fdProcess, &QProcess::finished, this, &SearchService::handleFdFinished);

    m_fdProcess->setProgram(m_fdPath);
    m_fdProcess->setArguments(args);
    m_fdProcess->start();
}

void SearchService::handleFdOutput()
{
    if (!m_fdProcess || !m_model) return;

    QList<QFileInfo> batch;
    while (m_fdProcess->canReadLine()) {
        QString line = QString::fromUtf8(m_fdProcess->readLine()).trimmed();
        if (line.isEmpty()) continue;
        batch.append(QFileInfo(line));

        if (batch.size() >= 50) {
            m_model->addResults(batch);
            m_resultCount += batch.size();
            emit resultCountChanged();
            batch.clear();
        }
    }
    if (!batch.isEmpty()) {
        m_model->addResults(batch);
        m_resultCount += batch.size();
        emit resultCountChanged();
    }
}

void SearchService::handleFdFinished()
{
    if (m_fdProcess && m_model) {
        QList<QFileInfo> batch;
        QByteArray remaining = m_fdProcess->readAllStandardOutput();
        for (const QByteArray &line : remaining.split('\n')) {
            QString path = QString::fromUtf8(line).trimmed();
            if (path.isEmpty()) continue;
            batch.append(QFileInfo(path));
        }
        if (!batch.isEmpty()) {
            m_model->addResults(batch);
            m_resultCount += batch.size();
            emit resultCountChanged();
        }
    }

    if (m_fdProcess) {
        m_fdProcess->deleteLater();
        m_fdProcess = nullptr;
    }

    m_searching = false;
    emit isSearchingChanged();
    emit searchFinished();
}

void SearchService::startIteratorSearch(const QString &rootPath, const QString &pattern, bool showHidden)
{
    m_workerThread = new QThread;
    m_worker = new SearchWorker;
    m_worker->moveToThread(m_workerThread);

    connect(m_worker, &SearchWorker::resultsReady, this, [this](const QList<QFileInfo> &entries) {
        if (m_model) {
            m_model->addResults(entries);
            m_resultCount += entries.size();
            emit resultCountChanged();
        }
    });

    connect(m_worker, &SearchWorker::finished, this, [this]() {
        m_searching = false;
        emit isSearchingChanged();
        emit searchFinished();

        if (m_workerThread) {
            m_workerThread->quit();
            m_workerThread->wait(2000);
            m_workerThread->deleteLater();
            m_workerThread = nullptr;
            m_worker = nullptr;
        }
    });

    connect(m_workerThread, &QThread::started, m_worker, [this, rootPath, pattern, showHidden]() {
        m_worker->search(rootPath, pattern, showHidden, m_maxResults);
    });

    m_workerThread->start();
}
