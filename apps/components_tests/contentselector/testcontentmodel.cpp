#include <components/contentselector/model/contentmodel.hpp>
#include <components/contentselector/model/esmfile.hpp>
#include <components/esm3/esmwriter.hpp>

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <sstream>

namespace
{
    struct ContentModelTest : testing::Test
    {
        QIcon mIcon;
        ContentSelectorModel::ContentModel mModel{ nullptr, mIcon, mIcon, true };
        QTemporaryDir mLow;
        QTemporaryDir mHigh;

        void write(const QString& path, const QByteArray& bytes = {})
        {
            QFile file(path);
            ASSERT_TRUE(file.open(QIODevice::WriteOnly));
            ASSERT_EQ(file.write(bytes), bytes.size());
        }
    };

    TEST_F(ContentModelTest, HigherPriorityDirectoryReplacesTheSameCaseInsensitiveReference)
    {
        write(mLow.filePath("Example.omwscripts"));
        write(mHigh.filePath("EXAMPLE.omwscripts"));
        mModel.addFiles(mLow.path(), false);
        const auto* original = mModel.item("example.omwscripts");
        ASSERT_NE(original, nullptr);
        mModel.addFiles(mHigh.path(), false);
        EXPECT_EQ(mModel.rowCount(), 1);
        EXPECT_EQ(mModel.item("Example.omwscripts"), original);
        EXPECT_EQ(original->filePath(), mHigh.filePath("EXAMPLE.omwscripts"));
        EXPECT_EQ(mModel.item(original->filePath()), original);
        EXPECT_EQ(mModel.item(mLow.filePath("Example.omwscripts")), nullptr);
    }

    TEST_F(ContentModelTest, NameIndexSurvivesReorderingAndFindsMissingDependencies)
    {
        write(mLow.filePath("a.omwscripts"));
        write(mLow.filePath("b.omwscripts"));
        mModel.addFiles(mLow.path(), false);
        const auto* a = mModel.item("a.omwscripts");
        const auto* b = mModel.item("b.omwscripts");
        // Exercise model reordering without setContentList's GUI progress dialog:
        // components-tests deliberately does not create a QApplication.
        mModel.setNonUserContent({ "b.omwscripts", "a.omwscripts", "Missing.esm" });
        EXPECT_EQ(mModel.item(0), b);
        EXPECT_EQ(mModel.item(1), a);
        EXPECT_EQ(mModel.item("A.OMWSCRIPTS"), a);
        EXPECT_EQ(mModel.item("B.OMWSCRIPTS"), b);
        ASSERT_NE(mModel.item("MISSING.ESM"), nullptr);
        EXPECT_TRUE(mModel.item("Missing.esm")->isMissing());
        EXPECT_TRUE(mModel.item("Missing.esm")->fromAnotherConfigFile());
    }

    TEST_F(ContentModelTest, EditingANameRemovesTheOldLookup)
    {
        write(mLow.filePath("old.omwscripts"));
        mModel.addFiles(mLow.path(), false);
        const auto* original = mModel.item("old.omwscripts");
        ASSERT_NE(original, nullptr);
        QStringList properties;
        for (int i = 0; i < ContentSelectorModel::EsmFile::FileProperty_GameFile; ++i)
            properties.append(original->fileProperty(static_cast<ContentSelectorModel::EsmFile::FileProperty>(i)).toString());
        properties[0] = "renamed.omwscripts";
        ASSERT_TRUE(mModel.setData(mModel.index(0, 0), properties));
        EXPECT_EQ(mModel.item("old.omwscripts"), nullptr);
        EXPECT_EQ(mModel.item("RENAMED.OMWSCRIPTS"), original);
    }

    TEST_F(ContentModelTest, ClearAndRescanObservesAddedAndRemovedFiles)
    {
        write(mLow.filePath("old.omwscripts"));
        mModel.addFiles(mLow.path(), false);
        EXPECT_FALSE(mModel.containsDataFiles(mLow.path()));
        mModel.clearFiles();
        EXPECT_EQ(mModel.item("old.omwscripts"), nullptr);
        ASSERT_TRUE(QFile::remove(mLow.filePath("old.omwscripts")));
        std::ostringstream stream;
        ESM::ESMWriter writer;
        writer.save(stream);
        const auto data = stream.str();
        write(mLow.filePath("new.esp"), QByteArray(data.data(), static_cast<int>(data.size())));
        mModel.addFiles(mLow.path(), false);
        EXPECT_TRUE(mModel.containsDataFiles(mLow.path()));
        ASSERT_NE(mModel.item("NEW.ESP"), nullptr);
        EXPECT_EQ(mModel.rowCount(), 1);
    }
}
