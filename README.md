# paper-io-numworks
paperio4numworks

# Paper.io - NumWorks

Fait par derim .gg/derim

Une adaptation du célèbre jeu Paper.io conçue spécifiquement pour la calculatrice NumWorks (compatible avec le firmware Epsilon via l'EADK).

Commandes :
- Flèches directionnelles : Diriger le joueur / changer de direction
- OK / EXE : Relancer une partie (après un Game Over ou une Victoire)
- Back / Home : Quitter l'application

Compilation :
Le projet utilise une chaîne de compilation bare-metal en C (arm-none-eabi-gcc).
Prérequis : arm-none-eabi-gcc, make, Node.js / npx (pour l'outil nwlink).

Ouvre un terminal à la racine du projet et lance :
make clean
make

Le fichier exécutable .nwa sera généré dans le dossier build/ : build/paper.nwa.

Installation sur la calculatrice :
Pour installer l'application sur ta NumWorks, connecte ta calculatrice en USB à ton ordinateur et utilise nwlink pour l'installer directement :
make run
(Ou utilise l'outil d'installation web officiel en glissant le fichier .nwa généré).
