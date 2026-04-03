#include "services/searchservice.h"
#include "models/searchresultsmodel.h"
#include <QStandardPaths>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDebug>

// ── SearchWorker (runs in separate thread) ──

SearchWorker::SearchWorker(QObject *parent) : QObject(parent) {}

void SearchWorker::search(const QString &rootPath, const QString &pattern, bool showHidden, int maxResults)
{
    m_cancelled = false;
    int count = 0;
    const QDir rootDir(rootPath);

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
        const QString name = info.fileName();
        const QString relativePath = rootDir.relativeFilePath(info.filePath());

        const bool globMatchesPath = pattern.contains('/');
        bool matches = isGlob ? regex.match(globMatchesPath ? relativePath : name).hasMatch()
                              : relativePath.contains(pattern, Qt::CaseInsensitive);
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

    qInfo().nospace() << "Search[" << objectName() << "] start root=" << rootPath
                      << " query='" << pattern << "' backend=" << (hasFd() ? "fd" : "iterator")
                      << " hidden=" << showHidden;

    if (hasFd())
        startFdSearch(rootPath, pattern, showHidden);
    else
        startIteratorSearch(rootPath, pattern, showHidden);
}

void SearchService::cancelSearch()
{
    const bool hadActiveSearch = m_searching || m_fdProcess || m_workerThread;

    if (m_fdProcess) {
        m_fdProcess->kill();
        m_fdProcess->waitForFinished(1000);
        delete m_fdProcess;
        m_fdProcess = nullptr;
    }
    m_fdRootPath.clear();

    SearchWorker *worker = m_worker;
    QThread *thread = m_workerThread;
    m_worker = nullptr;
    m_workerThread = nullptr;

    if (worker)
        worker->cancel();
    if (thread)
        thread->quit();

    if (m_searching) {
        m_searching = false;
        emit isSearchingChanged();
    }

    if (hadActiveSearch)
        qInfo().nospace() << "Search[" << objectName() << "] cancel";
}

void SearchService::startFdSearch(const QString &rootPath, const QString &pattern, bool showHidden)
{
    m_fdProcess = new QProcess(this);
    m_fdRootPath = rootPath;

    QStringList args;
    args << "--color" << "never" << "--follow";
    if (showHidden) args << "--hidden";
    args << "--max-results" << QString::number(m_maxResults);

    bool isGlob = pattern.contains('*') || pattern.contains('?') || pattern.contains('[');
    if (isGlob) {
        if (pattern.contains('/'))
            args << "--full-path";
        args << "--glob" << pattern;
    } else {
        args << "--full-path";
        args << "--fixed-strings" << pattern;
    }
    args << ".";

    connect(m_fdProcess, &QProcess::readyReadStandardOutput, this, &SearchService::handleFdOutput);
    connect(m_fdProcess, &QProcess::finished, this, &SearchService::handleFdFinished);

    m_fdProcess->setProgram(m_fdPath);
    m_fdProcess->setArguments(args);
    m_fdProcess->setWorkingDirectory(rootPath);
    m_fdProcess->start();
}

void SearchService::handleFdOutput()
{
    if (!m_fdProcess || !m_model) return;

    QList<QFileInfo> batch;
    const QDir rootDir(m_fdRootPath);
    while (m_fdProcess->canReadLine()) {
        QString line = QString::fromUtf8(m_fdProcess->readLine()).trimmed();
        if (line.isEmpty()) continue;
        batch.append(QFileInfo(QDir::cleanPath(rootDir.filePath(line))));

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
        const QDir rootDir(m_fdRootPath);
        QByteArray remaining = m_fdProcess->readAllStandardOutput();
        for (const QByteArray &line : remaining.split('\n')) {
            QString path = QString::fromUtf8(line).trimmed();
            if (path.isEmpty()) continue;
            batch.append(QFileInfo(QDir::cleanPath(rootDir.filePath(path))));
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
    m_fdRootPath.clear();

    m_searching = false;
    emit isSearchingChanged();
    qInfo().nospace() << "Search[" << objectName() << "] finish results=" << m_resultCount;
    emit searchFinished();
}

void SearchService::startIteratorSearch(const QString &rootPath, const QString &pattern, bool showHidden)
{
    QThread *thread = new QThread;
    SearchWorker *worker = new SearchWorker;
    worker->moveToThread(thread);

    m_workerThread = thread;
    m_worker = worker;

    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    connect(worker, &SearchWorker::resultsReady, this, [this, worker](const QList<QFileInfo> &entries) {
        if (worker != m_worker || !m_model)
            return;

        m_model->addResults(entries);
        m_resultCount += entries.size();
        emit resultCountChanged();
    });

    connect(worker, &SearchWorker::finished, thread, &QThread::quit);

    connect(worker, &SearchWorker::finished, this, [this, thread, worker]() {
        if (thread != m_workerThread || worker != m_worker)
            return;

        m_workerThread = nullptr;
        m_worker = nullptr;
        m_searching = false;
        emit isSearchingChanged();
        qInfo().nospace() << "Search[" << objectName() << "] finish results=" << m_resultCount;
        emit searchFinished();
    });

    connect(thread, &QThread::started, worker, [this, worker, rootPath, pattern, showHidden]() {
        worker->search(rootPath, pattern, showHidden, m_maxResults);
    });

    thread->start();
}
