# Run with `brew ruby` against generated fixtures. No real app or Keychain changes.
require "cask/cask_loader"
require "cask/installer"
require "fileutils"
require "shellwords"
require "tmpdir"

wrapper = Pathname(ARGV.fetch(0)).realpath
fixtures = ARGV[1] ? Pathname(ARGV[1]) : Tap.fetch("mg-chao/preflight").path/"Casks"

Dir.mktmpdir("snow-homebrew-installer-") do |directory|
  temporary = Pathname(directory)
  config = Cask::Config.new(explicit: { appdir: temporary/"Applications" })
  state = temporary/"retained-signing-state"
  state.write("persistent identity")

  %w[snow-shot snow-shot@beta snow-shot-mini snow-shot-mini@beta].each do |token|
    # Isolate Caskroom metadata as well as the configured app directory. Only the
    # fixture token changes; Homebrew evaluates and executes the generated DSL.
    fixture_token = "#{token.tr('@', '-')}-installer-fixture-#{Process.pid}"
    source = (fixtures/"#{token}.rb").read.sub("cask \"#{token}\"", "cask \"#{fixture_token}\"")
    load_cask = ->(content) { Cask::CaskLoader::FromContentLoader.new(content).load(config: config) }
    cask = load_cask.call(source)
    root = cask.caskroom_path
    raise "Refusing to overwrite an existing Caskroom fixture" if root.exist? || root.symlink?

    begin
      edition = token.start_with?("snow-shot-mini") ? "mini" : "full"
      slug = edition == "mini" ? "snow-shot-mini" : "snow-shot"
      name = edition == "mini" ? "Snow Shot Mini" : "Snow Shot"

      prepare = lambda do |item, fail: false|
        stage = item.staged_path
        stage.mkpath
        item.metadata_versioned_path.mkpath
        FileUtils.cp(wrapper, stage/wrapper.basename)
        if fail
          (stage/"install-snow-shot-macos.sh").write("#!/bin/bash\nexit 23\n")
          next
        end
        dmg = "#{slug}-#{item.version}-macos-#{item.arch || 'arm64'}.dmg"
        (stage/"install-snow-shot-macos.sh").write(<<~BASH)
          #!/bin/bash
          set -eu
          if [[ "${BASH_SOURCE[0]}" != "$0" ]]; then
            verify_project_application() { codesign --verify --deep --strict "$1"; }
            return 0
          fi
          test "$HOME" = #{Shellwords.escape(Dir.home)}
          test "$1" = --edition
          test "$2" = #{edition}
          test "$3" = --dmg
          test "$4" = #{Shellwords.escape((stage/dmg).to_s)}
          test "$5" = --prepare-app
          test "$6" = #{Shellwords.escape((stage/"#{name}.app").to_s)}
          test "$(cat #{Shellwords.escape(state.to_s)})" = 'persistent identity'
          mkdir -p "$6/Contents/MacOS" "$6/Contents/Resources"
          cp /usr/bin/true "$6/Contents/MacOS/fixture"
          cat > "$6/Contents/Info.plist" <<'PLIST'
          <?xml version="1.0" encoding="UTF-8"?>
          <plist version="1.0"><dict>
          <key>CFBundleExecutable</key><string>fixture</string>
          <key>CFBundleIdentifier</key><string>com.snowshot.installer-fixture</string>
          <key>CFBundlePackageType</key><string>APPL</string>
          <key>CFBundleVersion</key><string>1</string>
          </dict></plist>
          PLIST
          printf '%s\\n' #{Shellwords.escape(item.version.to_s)} > "$6/Contents/Resources/version"
          # Ad-hoc signing of a disposable fixture needs no Keychain identity.
          codesign --force --sign - "$6"
        BASH
      end

      artifacts = cask.artifacts.to_a
      installer = artifacts.find { |artifact| artifact.is_a?(Cask::Artifact::Installer) }
      app = artifacts.find { |artifact| artifact.is_a?(Cask::Artifact::App) }
      raise "Missing vendor installer or app artifact" unless installer && app
      raise "Preparation must precede app installation" unless artifacts.index(installer) < artifacts.index(app)
      raise "Preparation must fail on errors without sudo" if !installer.args[:must_succeed] || installer.requires_sudo?
      if artifacts.any? { |artifact| artifact.is_a?(Cask::Artifact::AbstractFlightBlock) }
        raise "Deprecated Ruby hook remains"
      end

      target = config.appdir/"#{name}.app"
      installed_version = target/"Contents/Resources/version"
      prepare.call(cask)
      engine = Cask::Installer.new(cask)
      engine.install_artifacts
      unless installed_version.read.strip == cask.version.to_s
        raise "Fresh installation did not place the prepared app"
      end

      # A normal upgrade prepares the new version in the same account, then
      # discards the old backup only after installation succeeds.
      upgraded_source = source.sub(/^  version .+$/, '  version "2096.0.1"')
      upgraded = load_cask.call(upgraded_source)
      upgraded_engine = Cask::Installer.new(upgraded)
      engine.start_upgrade(successor: upgraded)
      prepare.call(upgraded)
      upgraded_engine.install_artifacts(predecessor: cask)
      engine.purge_backed_up_versioned_files
      raise "Upgrade did not install the new version" unless installed_version.read.strip == upgraded.version.to_s

      # Failed successor preparation must restore the verified predecessor.
      # Signature identity itself is covered by the native signing fixture.
      successor = load_cask.call(source.sub(/^  version .+$/, '  version "2096.0.2"'))
      successor_engine = Cask::Installer.new(successor)
      upgraded_engine.start_upgrade(successor: successor)
      prepare.call(successor, fail: true)
      begin
        successor_engine.install_artifacts(predecessor: upgraded)
        raise "Failed preparation unexpectedly succeeded"
      rescue ErrorDuringExecution => error
        raise "Preparation failure was not propagated" unless error.status.exitstatus == 23
      end
      # Homebrew may retain an empty destination directory during upgrades to
      # preserve macOS app-management metadata.
      raise "Failed preparation placed an app" if (target/"Contents").exist?
      upgraded_engine.revert_upgrade(predecessor: successor)
      raise "Rollback did not restore the previous app" unless installed_version.read.strip == upgraded.version.to_s

      upgraded_engine.uninstall_artifacts
      raise "Uninstall left the installed app" if target.exist?
      raise "Uninstall removed persistent signing state" unless state.read == "persistent identity"
      puts "#{token}: native preparation, upgrade, failed-upgrade rollback, and uninstall passed."
    ensure
      FileUtils.remove_entry(root) if root.directory?
    end
  end
end
