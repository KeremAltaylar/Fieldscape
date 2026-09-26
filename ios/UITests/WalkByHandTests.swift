// The listener screens on the simulator: a tap on the map walks by hand (MapLibre's own recognisers
// once swallowed it - Kerem's iPhone 8, 2026-09-26), a press-and-drag keeps walking; the top bar's
// place picker opens Places, a route there frames it and a tap on it plays it; Layers switches the
// base map; Stop turns into Sound. Screenshots of each sheet are kept in the result bundle.
//   xcodebuild test -project ios/Fieldscape.xcodeproj -scheme Fieldscape -destination 'platform=iOS Simulator,name=Fieldscape iPhone'
import XCTest

final class WalkByHandTests: XCTestCase {
    private func shot(_ app: XCUIApplication, _ name: String) {
        let a = XCTAttachment(screenshot: app.screenshot()); a.name = name; a.lifetime = .keepAlways; add(a)
    }

    func testListenerScreens() {
        let app = XCUIApplication()
        addUIInterruptionMonitor(withDescription: "location") { alert in
            for b in ["Allow While Using App", "Allow Once", "Allow"] where alert.buttons[b].exists { alert.buttons[b].tap(); return true }
            return false
        }
        app.launch()
        sleep(8)                                    /* features and map style load */
        app.tap()                                   /* lets the interruption monitor run */
        let map = app.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.35))
        map.tap()
        XCTAssertTrue(app.staticTexts["BY HAND"].waitForExistence(timeout: 5), "a tap on the map should walk by hand")
        map.press(forDuration: 0.6, thenDragTo: app.coordinate(withNormalizedOffset: CGVector(dx: 0.6, dy: 0.3)))
        XCTAssertTrue(app.staticTexts["BY HAND"].exists)

        /* Places: search, parks, routes */
        app.buttons["Places and routes"].tap()
        XCTAssertTrue(app.staticTexts["PARKS"].waitForExistence(timeout: 3), "the Places sheet lists parks")
        shot(app, "places")
        let route = app.buttons.containing(NSPredicate(format: "label CONTAINS 'Koşuyolu Parkı' AND label CONTAINS ' m'")).firstMatch
        XCTAssertTrue(route.waitForExistence(timeout: 3), "the Places sheet lists the route")
        route.tap()
        sleep(3)                                    /* the map flies to the route */
        app.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.38)).tap()   /* the framed route's middle */
        let playing = app.staticTexts.containing(NSPredicate(format: "label BEGINSWITH 'Route Koşuyolu'")).firstMatch
        XCTAssertTrue(playing.waitForExistence(timeout: 8), "standing on the route plays it")
        sleep(4)                                    /* the first chord */
        shot(app, "walk")

        /* Layers: the base map */
        app.buttons["Map layers"].tap()
        XCTAssertTrue(app.buttons["Topo"].waitForExistence(timeout: 3))
        app.buttons["Topo"].tap()
        app.buttons["Zones"].tap()
        sleep(3)
        shot(app, "layers")
        app.buttons["Close"].tap()

        app.buttons["Stop the sound"].tap()
        XCTAssertTrue(app.buttons["Play the sound"].waitForExistence(timeout: 3), "Stop turns into Sound")
    }
}
