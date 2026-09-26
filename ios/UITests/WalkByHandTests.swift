// The listener screens on the simulator (Test 6): Routes lists the published routes and opens a route's
// card; press-and-hold on the map puts the walker there (a tap now opens what is under it), standing
// on the route plays it; a point in the walk panel opens its card, where Listen solos it and the
// recording plays as it was made; Layers switches the base map; Stop turns into Sound. Screenshots
// of each screen are kept in the result bundle.
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

        /* Routes: the published routes; one opens its card and frames the route */
        app.buttons["Routes"].tap()
        let route = app.buttons.containing(NSPredicate(format: "label CONTAINS 'Koşuyolu Parkı' AND label CONTAINS ' m'")).firstMatch
        XCTAssertTrue(route.waitForExistence(timeout: 3), "Routes lists the route")
        shot(app, "routes")
        route.tap()
        XCTAssertTrue(app.buttons["Show whole route"].waitForExistence(timeout: 3), "a route opens its card")
        sleep(3)                                    /* the map flies to the route */
        shot(app, "route card")
        app.buttons["Back to the walk"].tap()

        /* press and hold on the framed route: the walker goes there, and the route plays */
        app.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.38)).press(forDuration: 0.8)
        XCTAssertTrue(app.staticTexts["BY HAND"].waitForExistence(timeout: 5), "press and hold puts the walker on the map")
        let playing = app.staticTexts.containing(NSPredicate(format: "label BEGINSWITH 'Route Koşuyolu'")).firstMatch
        XCTAssertTrue(playing.waitForExistence(timeout: 8), "standing on the route plays it")
        sleep(4)                                    /* the first chord lights its segment */
        shot(app, "walk")

        /* the nearest point in the panel opens its card; Listen solos it; the recording plays */
        let point = app.buttons.containing(NSPredicate(format: "label BEGINSWITH 'Stretch'")).firstMatch
        XCTAssertTrue(point.waitForExistence(timeout: 5), "the panel lists the nearest points")
        point.tap()
        XCTAssertTrue(app.buttons["Listen"].waitForExistence(timeout: 3), "a point opens its card with Listen")
        app.buttons["Listen"].tap()
        XCTAssertTrue(app.buttons["Stop listening"].waitForExistence(timeout: 3), "Listen turns into Stop listening")
        app.buttons["Play the recording as it was made"].tap()
        XCTAssertTrue(app.buttons["Pause the recording"].waitForExistence(timeout: 30), "the recording plays")
        sleep(3)
        shot(app, "point card")
        app.buttons["Pause the recording"].tap()
        app.buttons["Stop listening"].tap()
        app.buttons["Back to the walk"].tap()

        /* Layers: the base map */
        app.buttons["Map layers"].tap()
        XCTAssertTrue(app.buttons["Topo"].waitForExistence(timeout: 3))
        app.buttons["Topo"].tap()
        sleep(3)
        shot(app, "layers")
        app.buttons["Close"].tap()

        app.buttons["Stop the sound"].tap()
        XCTAssertTrue(app.buttons["Play the sound"].waitForExistence(timeout: 3), "Stop turns into Sound")
    }
}
