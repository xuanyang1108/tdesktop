# Grok employees panel

The right-hand column named Grok 员工. `Panel` is the window column, `Storage` keeps employees, transcripts and the xAI key in `tdata/grok_employees.json`, `Agent` talks to `https://api.x.ai/v1/chat/completions`, and `RunTool` performs the Telegram actions through the open session.

Write actions (`send_message`, `forward_message`) wait for the in-panel confirm button, and they do nothing while 允许操作 TG is off. The column is created from `MainWidget` and docked by `MainWindow::setGrokPanel`. Leave chat lists, history storage and the send pipeline where they already live.
