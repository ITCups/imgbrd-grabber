#ifndef IMAGE_DOWNLOADER_H
#define IMAGE_DOWNLOADER_H

#include <QList>
#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include "downloader/file-downloader.h"
#include "downloader/image-save-result.h"
#include "filename/filename.h"
#include "models/image.h"
#include "network/network-reply.h"


class Blacklist;
class Profile;

class ImageDownloader : public QObject
{
	Q_OBJECT

	public:
		ImageDownloader(Profile *profile, QSharedPointer<Image> img, QString filename, QString path, int count, bool addMd5, bool startCommands, QObject *parent = nullptr, bool loadTags = true, bool rotate = true, bool force = false, Image::Size size = Image::Size::Unknown, bool postSave = true, bool forceExisting = false);
		ImageDownloader(Profile *profile, QSharedPointer<Image> img, QStringList paths, int count, bool addMd5, bool startCommands, QObject *parent = nullptr, bool rotate = true, bool force = false, Image::Size size = Image::Size::Unknown, bool postSave = true, bool forceExisting = false);
		~ImageDownloader() override;
		bool isRunning() const;
		void setSize(Image::Size size);
		void setBlacklist(Blacklist *blacklist);

	public slots:
		void save();
		void abort();

	protected:
		int needExactTags(QSettings *settings) const;
		Image::Size currentSize() const;
		QList<ImageSaveResult> makeResult(const QStringList &paths, Image::SaveResult result) const;
		QList<ImageSaveResult> afterTemporarySave(Image::SaveResult saveResult);

	signals:
		void downloadProgress(QSharedPointer<Image> img, qint64 v1, qint64 v2);
		void saved(QSharedPointer<Image> img, const QList<ImageSaveResult> &result);

	private slots:
		void loadedSave(Image::LoadTagsResult result);
		void loadImage(bool rateLimit = false);
		void downloadProgressImage(qint64 v1, qint64 v2);
		void writeError();
		void networkError(NetworkReply::NetworkError error, const QString &msg);
		void success();
		void checkStall();

	private:
		Profile *m_profile;
		Blacklist *m_blacklist = nullptr;
		QSharedPointer<Image> m_image;
		FileDownloader m_fileDownloader;
		Filename m_filename;
		QString m_path;
		bool m_loadTags;
		QStringList m_paths;
		QString m_temporaryPath;
		int m_count;
		bool m_addMd5;
		bool m_startCommands;
		bool m_writeError;
		bool m_rotate;
		bool m_force;
		Image::Size m_size = Image::Size::Unknown;
		bool m_postSave;
		bool m_forceExisting;

		NetworkReply *m_reply = nullptr;
		QUrl m_url;
		bool m_tryingSample = false;
		QTimer *m_stallTimer = nullptr;
		qint64 m_lastBytesReceived = 0;
		int m_stallRetries = 0;
		static constexpr int MAX_STALL_RETRIES = 2;
		static constexpr int STALL_TIMEOUT_MS = 30000;  // 30 seconds
};

#endif // IMAGE_DOWNLOADER_H
