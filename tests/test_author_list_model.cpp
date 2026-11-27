#include <catch2/catch_test_macros.hpp>

#include "author_list_model.hpp"

#include <QSignalSpy>
#include <QVariantMap>

using seventhslide::app::AuthorListModel;

namespace {

QVariantMap credit(const QString& type, const QString& label = {})
{
    return {{QStringLiteral("type"), type},
            {QStringLiteral("label"), label},
            {QStringLiteral("lang"), QString()}};
}

QVariantMap author(const QString& name, const QVariantList& roles = {})
{
    return {{QStringLiteral("name"), name}, {QStringLiteral("roles"), roles}};
}

QStringList credit_types(const AuthorListModel& model, int row)
{
    QStringList out;
    for (const auto& c : model.credits_at(row)) {
        const auto m = c.toMap();
        out.push_back(m.value(QStringLiteral("type")).toString() + QLatin1Char(':')
                      + m.value(QStringLiteral("label")).toString());
    }
    return out;
}

} // namespace

TEST_CASE("AuthorListModel loads a song's authors as the editor shows them", "[app][authors]")
{
    AuthorListModel model;
    model.load({author(QStringLiteral("  John Newton "),
                       {credit(QStringLiteral("words")), QVariantMap{{QStringLiteral("label"), QStringLiteral("x")}},
                        credit(QStringLiteral("other"), QStringLiteral("Paraphrase"))}),
                author(QStringLiteral("   ")),
                author(QStringLiteral("Anon"))});

    REQUIRE(model.count() == 2);
    CHECK(model.nameAt(0) == QStringLiteral("John Newton"));
    CHECK(credit_types(model, 0) == QStringList{QStringLiteral("words:"), QStringLiteral("other:Paraphrase")});
    CHECK(model.data(model.index(1), AuthorListModel::NameRole).toString() == QStringLiteral("Anon"));
    CHECK(model.data(model.index(0), AuthorListModel::CreditsRole).toList().size() == 2);
    CHECK(model.nameAt(5).isEmpty());
    CHECK(model.credits_at(-1).isEmpty());
}

TEST_CASE("AuthorListModel credits toggle as a set", "[app][authors]")
{
    AuthorListModel model;
    model.load({author(QStringLiteral("A"))});
    QSignalSpy changed(&model, &AuthorListModel::dataChanged);

    model.toggleCredit(0, QStringLiteral("music"));
    model.toggleCredit(0, QStringLiteral("words"));
    CHECK(credit_types(model, 0) == QStringList{QStringLiteral("music:"), QStringLiteral("words:")});
    model.toggleCredit(0, QStringLiteral("music"));
    CHECK(credit_types(model, 0) == QStringList{QStringLiteral("words:")});
    CHECK(changed.count() == 3);

    // Typing a free-text credit twice adds it once; the menu row toggles it.
    model.addCustomCredit(0, QStringLiteral(" Descant "));
    model.addCustomCredit(0, QStringLiteral("Descant"));
    model.addCustomCredit(0, QStringLiteral("  "));
    CHECK(credit_types(model, 0) == QStringList{QStringLiteral("words:"), QStringLiteral("other:Descant")});
    model.toggleCustomCredit(0, QStringLiteral("Descant"));
    CHECK(credit_types(model, 0) == QStringList{QStringLiteral("words:")});
    model.toggleCustomCredit(0, QStringLiteral("Descant"));
    CHECK(credit_types(model, 0) == QStringList{QStringLiteral("words:"), QStringLiteral("other:Descant")});
}

TEST_CASE("AuthorListModel saves named rows only", "[app][authors]")
{
    AuthorListModel model;
    QSignalSpy count(&model, &AuthorListModel::countChanged);
    model.append(QString());
    model.append(QStringLiteral("B"));
    model.setName(0, QStringLiteral(" A "));
    model.toggleCredit(1, QStringLiteral("arrangement"));
    model.append(QStringLiteral("  "));
    CHECK(count.count() == 3);

    const auto saved = model.authors();
    REQUIRE(saved.size() == 2);
    CHECK(saved[0].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("A"));
    CHECK(saved[1].toMap().value(QStringLiteral("roles")).toList().size() == 1);

    model.remove(0);
    CHECK(model.nameAt(0) == QStringLiteral("B"));
    model.remove(9);
    model.clear();
    CHECK(model.count() == 0);
}
