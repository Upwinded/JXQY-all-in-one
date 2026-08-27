#include "AboutDialog.h"
#include "core/BuildVersion.h"
#include "ui_AboutDialog.h"

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::AboutDialog)
{
    ui->setupUi(this);
    ui->versionLabel->setText(
        tr("版本 %1")
            .arg(BuildVersion::editorProductVersion()));

    QFont iconFont;
    iconFont.setPointSize(40);
    ui->iconLabel->setFont(iconFont);

    QFont titleFont;
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    ui->titleLabel->setFont(titleFont);
}

AboutDialog::~AboutDialog()
{
    delete ui;
}
