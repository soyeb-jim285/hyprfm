#pragma once

#include <QObject>
#include <QFileInfo>
#include <QProcess>
#include <QThread>

class SearchResultsModel;

class SearchWorker : public QObject
{
    Q_OBJECT
public:
    explicit SearchWorker(QObject *parent = nullptr);

    void search(const QString &rootPath, const QString &pattern, bool showHidden, int maxResults);
    void cancel();

signals:
    void resultsReady(const QList<QFileInfo> &entries);
    void finished();

private:
    std::atomic<bool> m_cancelled{false};
};

class SearchService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isSearching READ isSearching NOTIFY isSearchingChanged)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY resultCountChanged)
    Q_PROPERTY(int maxResults READ maxResults WRITE setMaxResults NOTIFY maxResultsChanged)

public:
    explicit SearchService(QObject *parent = nullptr);
    ~SearchService() override;

    void setResultsModel(SearchResultsModel *model);
    bool hasFd() const;
    bool isSearching() const;
    int resultCount() const;
    int maxResults() const;
    void setMaxResults(int max);

    Q_INVOKABLE void startSearch(const QString &rootPath, const QString &pattern, bool showHidden);
    Q_INVOKABLE void cancelSearch();

signals:
    void searchFinished();
    void isSearchingChanged();
    void resultCountChanged();
    void maxResultsChanged();

private:
    void startFdSearch(const QString &rootPath, const QString &pattern, bool showHidden);
    void startIteratorSearch(const QString &rootPath, const QString &pattern, bool showHidden);
    void handleFdOutput();
    void handleFdFinished();

    SearchResultsModel *m_model = nullptr;
    QProcess *m_fdProcess = nullptr;
    QThread *m_workerThread = nullptr;
    SearchWorker *m_worker = nullptr;
    QString m_fdPath;
    QString m_fdRootPath;
    bool m_searching = false;
    int m_resultCount = 0;
    int m_maxResults = 10000;
};
