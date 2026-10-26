#pragma once
#include <QFile>
#include <QSaveFile>
#include <QString>
#include <QTextStream>
#include <algorithm>
#include <utility>
#include <vector>

// One Rendering / DTM profile as a file: a single [vprenderer.<name>] section
// in the same "key: value" form as VideoProcessor.cfg, so it can also be
// pasted into a configuration by hand. Used to export, import and compare.
namespace ToneProfileFile
{
    struct Profile
    {
        QString section;
        std::vector<std::pair<QString, QString>> settings; // in file order
    };

    // A rendering profile section: [vprenderer] or [vprenderer.<name>].
    inline bool IsRenderingSection(const QString& section)
    {
        const QString lower = section.toLower();
        return lower == QStringLiteral("vprenderer") ||
            (lower.startsWith(QStringLiteral("vprenderer.")) && !lower.mid(11).contains(u'.') && lower.size() > 11);
    }

    inline bool Read(const QString& path, Profile& profile, QString& error)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            error = file.errorString();
            return false;
        }
        QTextStream stream(&file);
        profile = {};
        while (!stream.atEnd())
        {
            const QString line = stream.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(u'#') || line.startsWith(u';')) continue;
            if (line.startsWith(u'[') && line.endsWith(u']'))
            {
                if (!profile.section.isEmpty()) break; // one profile per file
                profile.section = line.mid(1, line.size() - 2).trimmed();
                continue;
            }
            if (profile.section.isEmpty()) continue;
            const int colon = line.indexOf(u':'), equals = line.indexOf(u'=');
            const int split = colon < 0 ? equals : equals < 0 ? colon : std::min(colon, equals);
            if (split <= 0) continue;
            profile.settings.emplace_back(line.left(split).trimmed().toLower(), line.mid(split + 1).trimmed());
        }
        if (!IsRenderingSection(profile.section))
        {
            error = QStringLiteral("The file holds no Rendering / DTM profile ([vprenderer.<name>]).");
            return false;
        }
        return true;
    }

    inline bool Write(const QString& path, const Profile& profile, QString& error)
    {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            error = file.errorString();
            return false;
        }
        QTextStream stream(&file);
        stream << "# VideoProcessor Rendering / DTM profile, exported by the Config GUI.\n"
               << "# Owned overrides and any legacy preset fallback are preserved.\n"
               << '[' << profile.section << "]\n";
        for (const auto& setting : profile.settings)
            stream << setting.first << ": " << setting.second << '\n';
        stream.flush();
        if (!file.commit())
        {
            error = file.errorString();
            return false;
        }
        return true;
    }
}
