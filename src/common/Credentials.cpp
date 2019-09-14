#include "Credentials.hpp"

#include "debug/AssertInGuiThread.hpp"
#include "keychain.h"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "util/CombinePath.hpp"

#include <QSaveFile>

#define FORMAT_NAME                                                  \
    ([&] {                                                           \
        assert(!provider.contains(":"));                             \
        return QString("chatterino:%1:%2").arg(provider).arg(name_); \
    })()

namespace chatterino {

namespace {
    bool useKeyring()
    {
        if (getPaths()->isPortable())
        {
            return false;
        }
        else
        {
#ifdef Q_OS_LINUX
            return getSettings()->useKeyring;
#else
            return true;
#endif
        }
    }

    // Insecure storage:
    QString insecurePath()
    {
        return combinePath(getPaths()->settingsDirectory, "credentials.json");
    }

    QJsonDocument loadInsecure()
    {
        QFile file(insecurePath());
        file.open(QIODevice::ReadOnly);
        return QJsonDocument::fromJson(file.readAll());
    }

    void storeInsecure(const QJsonDocument &doc)
    {
        QSaveFile file(insecurePath());
        file.open(QIODevice::WriteOnly);
        file.write(doc.toJson());
        file.commit();
    }

    QJsonDocument &insecureInstance()
    {
        static auto store = loadInsecure();
        return store;
    }

    void queueInsecureSave()
    {
        static bool isQueued = false;

        if (!isQueued)
        {
            isQueued = true;
            QTimer::singleShot(200, qApp, [] {
                storeInsecure(insecureInstance());
                isQueued = false;
            });
        }
    }
}  // namespace

Credentials &Credentials::getInstance()
{
    static Credentials creds;
    return creds;
}

Credentials::Credentials()
{
}

void Credentials::get(const QString &provider, const QString &name_,
                      std::function<void(const QString &)> &&onLoaded)
{
    assertInGuiThread();

    auto name = FORMAT_NAME;

    if (useKeyring())
    {
        auto job = new QKeychain::ReadPasswordJob("chatterino");
        job->setAutoDelete(true);
        job->setKey(name);
        QObject::connect(job, &QKeychain::Job::finished, qApp,
                         [job, onLoaded = std::move(onLoaded)](auto) mutable {
                             onLoaded(job->textData());
                         },
                         Qt::DirectConnection);
        job->start();
    }
    else
    {
        auto &instance = insecureInstance();

        onLoaded(instance.object().find(name).value().toString());
    }
}

void Credentials::set(const QString &provider, const QString &name_,
                      const QString &credential)
{
    assertInGuiThread();

    /// On linux, we try to use a keychain but show a message to disable it when it fails.
    /// XXX: add said message

    auto name = FORMAT_NAME;

    if (useKeyring())
    {
        auto job = new QKeychain::WritePasswordJob("chatterino");
        job->setAutoDelete(true);
        job->setKey(name);
        job->setTextData(credential);
        //QObject::connect(job, &QKeychain::Job::finished, qApp, [](auto) {});
        job->start();
    }
    else
    {
        auto &instance = insecureInstance();

        instance.object()[name] = credential;

        queueInsecureSave();
    }
}

void Credentials::erase(const QString &provider, const QString &name_)
{
    assertInGuiThread();

    auto name = FORMAT_NAME;

    if (useKeyring())
    {
        auto job = new QKeychain::DeletePasswordJob("chatterino");
        job->setAutoDelete(true);
        job->setKey(name);
        //QObject::connect(job, &QKeychain::Job::finished, qApp, [](auto) {});
        job->start();
    }
    else
    {
        auto &instance = insecureInstance();

        if (auto it = instance.object().find(name);
            it != instance.object().end())
        {
            instance.object().erase(it);
        }

        queueInsecureSave();
    }
}

}  // namespace chatterino
