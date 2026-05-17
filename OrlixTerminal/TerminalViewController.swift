import GhosttyTerminal
import GhosttyTheme
import UIKit

final class TerminalViewController: UIViewController {
    private static let lightThemeKey = "SelectedTheme.light"
    private static let darkThemeKey = "SelectedTheme.dark"

    private var didStartBoot = false
    private lazy var terminalView = TerminalView(frame: .zero)
    private lazy var terminalSession = InMemoryTerminalSession(
        write: { _ in },
        resize: { _ in }
    )
    private lazy var controller = TerminalController(
        theme: Self.savedTerminalTheme()
    ) { builder in
        builder.withBackgroundOpacity(0)
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        title = "Orlix"
        view.backgroundColor = .systemBackground
        view.isOpaque = true
        configureTerminalView()
        configureThemeMenu()
        applyBackgroundForCurrentAppearance()
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        activateTerminal()
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        terminalView.fitToSize()
    }

    override func traitCollectionDidChange(_ previousTraitCollection: UITraitCollection?) {
        super.traitCollectionDidChange(previousTraitCollection)
        guard traitCollection.hasDifferentColorAppearance(comparedTo: previousTraitCollection) else {
            return
        }
        controller.setTheme(Self.savedTerminalTheme())
        applyBackgroundForCurrentAppearance()
    }

    private func configureTerminalView() {
        terminalView.delegate = self
        terminalView.configuration = TerminalSurfaceOptions(
            backend: .inMemory(terminalSession)
        )
        terminalView.controller = controller
        terminalView.backgroundColor = .clear
        terminalView.isOpaque = false
        terminalView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(terminalView)

        NSLayoutConstraint.activate([
            terminalView.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor),
            terminalView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            terminalView.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            terminalView.bottomAnchor.constraint(equalTo: view.keyboardLayoutGuide.topAnchor),
        ])
    }

    private func activateTerminal() {
        terminalView.becomeFirstResponder()
        guard !didStartBoot else {
            return
        }
        didStartBoot = true

        terminalSession.receive("OrlixTerminal\r\n")
        guard let profile = Self.selectedBootProfileName() else {
            terminalSession.receive("Missing selected Orlix profile in packaged kernel payload.\r\n")
            return
        }
        terminalSession.receive("Starting Orlix bootloader with the \(Self.profileDisplayName(profile)) profile.\r\n")
        let status = profile.withCString { OrlixTerminalBootProfileNamed($0) }
        terminalSession.receive(
            String(cString: OrlixTerminalBootStatusMessage(status)) + "\r\n"
        )
    }

    private static func selectedBootProfileName() -> String? {
        guard let kernelBundle = kernelBundle(),
              let payloadBundle = kernelBundle.url(
                  forResource: "OrlixKernelPayload",
                  withExtension: "bundle"
              )
        else {
            return nil
        }

        let profileURL = payloadBundle.appendingPathComponent("selected_profile.txt")
        guard let profile = try? String(contentsOf: profileURL, encoding: .utf8)
            .trimmingCharacters(in: .whitespacesAndNewlines)
        else {
            return nil
        }

        switch profile {
        case "appstore", "development":
            return profile
        default:
            return nil
        }
    }

    private static func kernelBundle() -> Bundle? {
        if let loadedBundle = Bundle(identifier: "org.orlix.OrlixKernel") {
            return loadedBundle
        }
        if let loadedBundle = Bundle.allFrameworks.first(where: { $0.bundleIdentifier == "org.orlix.OrlixKernel" }) {
            return loadedBundle
        }
        guard let frameworksURL = Bundle.main.privateFrameworksURL else {
            return nil
        }

        let frameworkURL = frameworksURL.appendingPathComponent(
            "OrlixKernel.framework",
            isDirectory: true
        )
        return Bundle(url: frameworkURL)
    }

    private static func profileDisplayName(_ profile: String) -> String {
        switch profile {
        case "appstore":
            return "App Store"
        case "development":
            return "development"
        default:
            return profile
        }
    }

    private static func savedTerminalTheme() -> TerminalTheme {
        let lightConfig = savedThemeDefinition(forKey: lightThemeKey)?
            .toTerminalConfiguration() ?? .alabaster
        let darkConfig = savedThemeDefinition(forKey: darkThemeKey)?
            .toTerminalConfiguration() ?? .afterglow
        return TerminalTheme(light: lightConfig, dark: darkConfig)
    }

    private static func savedThemeDefinition(
        forKey key: String
    ) -> GhosttyThemeDefinition? {
        guard let name = UserDefaults.standard.string(forKey: key) else {
            return nil
        }
        return GhosttyThemeCatalog.theme(named: name)
    }

    private var isDarkMode: Bool {
        traitCollection.userInterfaceStyle == .dark
    }

    private func saveTheme(_ theme: GhosttyThemeDefinition) {
        let key = isDarkMode ? Self.darkThemeKey : Self.lightThemeKey
        UserDefaults.standard.set(theme.name, forKey: key)
    }

    private func applyBackgroundForCurrentAppearance() {
        let key = isDarkMode ? Self.darkThemeKey : Self.lightThemeKey
        guard let theme = Self.savedThemeDefinition(forKey: key) else {
            return
        }
        if let backgroundColor = UIColor(hexString: theme.background) {
            view.backgroundColor = backgroundColor
        }
    }

    private func configureThemeMenu() {
        navigationItem.rightBarButtonItem = UIBarButtonItem(
            image: UIImage(systemName: "paintpalette"),
            menu: buildThemeMenu()
        )
    }

    private func buildThemeMenu() -> UIMenu {
        let popular = buildSubmenu(
            title: "Popular",
            themes: [
                "Dracula", "Catppuccin Mocha", "Catppuccin Latte",
                "Nord", "Solarized Dark", "Solarized Light",
                "Gruvbox Dark", "Gruvbox Light", "Tokyo Night",
                "One Half Dark", "One Half Light", "Rose Pine",
                "Monokai Pro", "GitHub Dark", "GitHub Light",
            ]
        )

        let dark = UIMenu(
            title: "Dark",
            image: UIImage(systemName: "moon.fill"),
            children: alphabeticalSubmenus(
                themes: GhosttyThemeCatalog.allThemes.filter(\.isDark)
            )
        )

        let light = UIMenu(
            title: "Light",
            image: UIImage(systemName: "sun.max.fill"),
            children: alphabeticalSubmenus(
                themes: GhosttyThemeCatalog.allThemes.filter { !$0.isDark }
            )
        )

        return UIMenu(title: "Theme", children: [popular, dark, light])
    }

    private func buildSubmenu(
        title: String,
        themes names: [String]
    ) -> UIMenu {
        let actions = names.compactMap { name -> UIAction? in
            guard let theme = GhosttyThemeCatalog.theme(named: name) else {
                return nil
            }
            return themeAction(for: theme)
        }
        return UIMenu(
            title: title,
            image: UIImage(systemName: "star.fill"),
            children: actions
        )
    }

    private func alphabeticalSubmenus(
        themes: [GhosttyThemeDefinition]
    ) -> [UIMenu] {
        var grouped: [String: [GhosttyThemeDefinition]] = [:]
        for theme in themes {
            let letter = String(theme.name.prefix(1)).uppercased()
            let key = letter.first?.isLetter == true ? letter : "#"
            grouped[key, default: []].append(theme)
        }

        return grouped.keys.sorted().map { key in
            UIMenu(
                title: key,
                children: grouped[key]!.map { themeAction(for: $0) }
            )
        }
    }

    private func themeAction(for theme: GhosttyThemeDefinition) -> UIAction {
        UIAction(title: theme.name) { [weak self] _ in
            self?.applyTheme(theme)
        }
    }

    private func applyTheme(_ theme: GhosttyThemeDefinition) {
        saveTheme(theme)
        controller.setTheme(Self.savedTerminalTheme())

        if let backgroundColor = UIColor(hexString: theme.background) {
            view.backgroundColor = backgroundColor
        }
    }
}

extension TerminalViewController:
    TerminalSurfaceTitleDelegate,
    TerminalSurfaceResizeDelegate,
    TerminalSurfaceCloseDelegate
{
    func terminalDidChangeTitle(_ title: String) {
        self.title = title
    }

    func terminalDidResize(columns _: Int, rows _: Int) {}

    func terminalDidClose(processAlive _: Bool) {
        ApplicationExitController.requestExit()
    }
}

private extension UIColor {
    convenience init?(hexString: String) {
        let hex = hexString.hasPrefix("#") ? String(hexString.dropFirst()) : hexString
        guard hex.count == 6,
              let r = UInt8(hex.prefix(2), radix: 16),
              let g = UInt8(hex.dropFirst(2).prefix(2), radix: 16),
              let b = UInt8(hex.dropFirst(4), radix: 16)
        else {
            return nil
        }
        self.init(
            red: CGFloat(r) / 255,
            green: CGFloat(g) / 255,
            blue: CGFloat(b) / 255,
            alpha: 1
        )
    }
}
