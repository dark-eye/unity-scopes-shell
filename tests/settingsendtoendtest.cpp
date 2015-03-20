/*
 * Copyright (C) 2013-2014 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *  Pete Woods <pete.woods@canonical.com>
 */

#include <QTest>
#include <QScopedPointer>
#include <QSignalSpy>

#include <scopes.h>
#include <scope.h>

#include <unity/shell/scopes/SettingsModelInterface.h>

#include <scope-harness/registry/pre-existing-registry.h>
#include <scope-harness/test-utils.h>

using namespace scopes_ng;
using namespace unity::scopeharness;
using namespace unity::scopeharness::registry;
using namespace unity::shell::scopes;

class SettingsEndToEndTest : public QObject
{
    Q_OBJECT
private:
    QScopedPointer<Scopes> m_scopes;
    Scope::Ptr m_scope;
    Registry::UPtr m_registry;

private Q_SLOTS:
    void initTestCase()
    {
        m_registry.reset(new PreExistingRegistry(TEST_RUNTIME_CONFIG));
        m_registry->start();
    }

    void cleanupTestCase()
    {
        m_registry.reset();
    }

    void init()
    {
        const QStringList favs {"scope://mock-scope-departments", "scope://mock-scope-double-nav", "scope://mock-scope"};
        TestUtils::setFavouriteScopes(favs);

        m_scopes.reset(new Scopes(nullptr));
        // no scopes on startup
        QCOMPARE(m_scopes->rowCount(), 0);
        QCOMPARE(m_scopes->loaded(), false);
        QSignalSpy spy(m_scopes.data(), SIGNAL(loadedChanged()));
        // wait till the registry spawns
        QVERIFY(spy.wait());
        QCOMPARE(m_scopes->loaded(), true);
        // should have at least one scope now
        QVERIFY(m_scopes->rowCount() > 1);

        // get scope proxy
        m_scope = m_scopes->getScopeById("mock-scope");
        QVERIFY(m_scope != nullptr);
        m_scope->setActive(true);
    }

    void cleanup()
    {
        m_scopes.reset();
        m_scope.reset();
    }

    void verifySetting(const SettingsModelInterface* settings, int index,
            const QString& id, const QString& displayName, const QString& type,
            const QVariantMap& properties, const QVariant& value)
    {
        QCOMPARE(id, settings->data(settings->index(index), SettingsModelInterface::RoleSettingId).toString());
        QCOMPARE(displayName, settings->data(settings->index(index), SettingsModelInterface::RoleDisplayName).toString());
        QCOMPARE(type, settings->data(settings->index(index), SettingsModelInterface::RoleType).toString());

        QCOMPARE(properties, settings->data(settings->index(index), SettingsModelInterface::RoleProperties).toMap());
        QCOMPARE(value, settings->data(settings->index(index), SettingsModelInterface::RoleValue));
    }

    void testBasic()
    {
        const auto settings = m_scope->settings();
        QVERIFY(settings);

        // Wait for the settings model to initialize
        if(settings->count() == 0)
        {
            QSignalSpy settingsCountSpy(settings, SIGNAL(countChanged()));
            QVERIFY(settingsCountSpy.wait());
        }

        QCOMPARE(settings->count(), 5);

        verifySetting(settings, 0, "location", "Location", "string", QVariantMap({ {"defaultValue", "London"} }), "London");
        verifySetting(settings, 1, "distanceUnit", "Distance Unit", "list", QVariantMap({ {"defaultValue", 1}, {"values", QStringList({"Kilometers", "Miles"})} }), 1);
        verifySetting(settings, 2, "age", "Age", "number", QVariantMap({ {"defaultValue", 23} }), 23);
        verifySetting(settings, 3, "enabled", "Enabled", "boolean", QVariantMap({ {"defaultValue", true} }), true);
        verifySetting(settings, 4, "color", "Color", "string", QVariantMap({ {"defaultValue", QVariant()} }), QVariant());
    }

    void setValue(SettingsModelInterface *settings, int index, const QVariant& value)
    {
        QVERIFY(settings);

        QVERIFY(settings->setData(settings->index(index), value,
                SettingsModelInterface::RoleValue));
    }

    void verifySettingsChangedSignal()
    {
        auto settings = m_scope->settings();
        QVERIFY(settings);

        // Wait for the settings model to initialize
        if(settings->count() == 0)
        {
            QSignalSpy settingsCountSpy(settings, SIGNAL(countChanged()));
            QVERIFY(settingsCountSpy.wait());
        }

        QSignalSpy settingChangedSpy(settings, SIGNAL(settingsChanged()));

        // Before changing any value check that the results are not marked as dirty
        // Also set the scope as inactive so we can check that it gets marked as dirty
        m_scope->setActive(false);
        QCOMPARE(m_scope->isActive(), false);
        QCOMPARE(m_scope->resultsDirty(), false);

        // change a value and wait for the signal
        setValue(settings, 0, "Barcelona");
        QVERIFY(settingChangedSpy.wait());

        // Check that the results are dirty
        QCOMPARE(m_scope->resultsDirty(), true);
    }

    void testChildScopes()
    {
        // get aggregator scope proxy
        m_scope = m_scopes->getScopeById("mock-scope-departments");
        QVERIFY(m_scope != nullptr);
        m_scope->setActive(true);

        const auto settings = m_scope->settings();
        QVERIFY(settings);

        // Wait for the settings model to initialize
        if(settings->count() == 0)
        {
            QSignalSpy settingsCountSpy(settings, SIGNAL(countChanged()));
            QVERIFY(settingsCountSpy.wait());
        }

        QCOMPARE(settings->count(), 4);

        verifySetting(settings, 0, "string-setting", "String Setting", "string", QVariantMap({ {"defaultValue", "Hello"} }), "Hello");
        verifySetting(settings, 1, "number-setting", "Number Setting", "number", QVariantMap({ {"defaultValue", 13} }), 13);
        verifySetting(settings, 2, "mock-scope-double-nav", "Display results from mock-double-nav.DisplayName", "boolean", QVariantMap(), true);
        verifySetting(settings, 3, "mock-scope", "Display results from mock.DisplayName", "boolean", QVariantMap(), true);
    }
};

QTEST_GUILESS_MAIN(SettingsEndToEndTest)
#include <settingsendtoendtest.moc>
